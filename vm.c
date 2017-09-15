/*
 * Dummy VM
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>
#include <syscall.h>
#include <sys/mman.h>
#include <poll.h>
#include <inttypes.h>
#include <errno.h>

#include <limits.h>
#include <linux/perf_event.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>

#define LARGE 15000
#define HOT_THRESHOLD 1027
#define TRACE_OUTPUT "trace.data"

/* Debug helpers */
#define DPRINTF(who, x...)                                      \
    do {                                                        \
        fprintf(stderr, "[%s:%d](%s:%s:%d): ",              \
            who, getpid(), __func__, __FILE__, __LINE__);   \
        fprintf(stderr, x);                                 \
        fprintf(stderr, "\n");                              \
    } while (0)
#define TDEBUG(x...)    DPRINTF("TRACER", x);
#define VDEBUG(x...)    DPRINTF("VM", x);

/* mmaped buffer sizes (in pages). Must be powers of 2! */
#define DATA_PAGES 1024
#define AUX_PAGES 8192

/* Protos */
int trace_on(void);
void interpreter_loop(void);
void poll_loop(int, int, struct perf_event_mmap_page *, void *, int);
void read_circular_buf(void *, __u64, __u64, __u64 *, int);
void write_buf_to_disk(int, void *, __u64);
void stash_maps(void);

void
stash_maps(void)
{
    char *cmd;
    pid_t pid;
    int res;

    VDEBUG("Stash linker maps");

    pid = getpid();
    res = asprintf(&cmd, "cp /proc/%d/maps maps", pid);
    if (res == -1) {
        err(EXIT_FAILURE, "asprintf");
    }

    res = system(cmd);
    if (res != 0) {
        err(EXIT_FAILURE, "system");
    }
}

/* Linux poll(2) man page:
 *
 *     Some implementations define the nonstandard constant INFTIM with the
 *     value -1.
 */
#ifndef INFTIM
#define INFTIM -1
#endif

void write_buf_to_disk(int fd, void *buf, __u64 size) {
    __u64 written = 0;
    ssize_t res;

    while (written < size) {
        res = write(fd, buf, size);
        if (res == -1) {
            if (errno == EINTR) {
                /* Write interrupted before anything written */
                continue;
            }
            err(EXIT_FAILURE, "write");
        }
        written += res;
    }
}

/*
 * Read data out of a circular buffer
 *
 * Tail is passed as a pointer so we can mutate it.
 */
void
read_circular_buf(void *buf, __u64 size, __u64 head_monotonic, __u64 *tail_p, int out_fd) {
    __u64 tail = *tail_p;
    __u64 head = head_monotonic % size; /* head must be manually wrapped */

    if (tail <= head) {
        /* No wrap-around */
        TDEBUG("read with no wrap");
        write_buf_to_disk(out_fd, buf + tail, head - tail);
    } else {
        /* Wrap-around */
        TDEBUG("read with wrap");
        write_buf_to_disk(out_fd, buf + tail, size - tail);
        write_buf_to_disk(out_fd, buf, head);
    }

    /*
     * Update buffer tail, thus marking the space we just read as re-usable.
     * Note that the head is the buffer head at the time this func was called.
     * The head may have advanced since then, which is fine.
     */
    *tail_p = head;
}

/*
 * Take trace data out of the AUX buffer.
 */
void
poll_loop(int perf_fd, int out_fd, struct perf_event_mmap_page *mmap_header,
    void *aux, int stop_tracer_fd)
{
    struct pollfd pfds[2] = {
        {perf_fd, POLLIN | POLLHUP, 0},
        {stop_tracer_fd, POLLHUP, 0}
    };
    int n_events = 0;
    size_t num_wakes = 0;

    while (1) {
        n_events = poll(pfds, 2, INFTIM);
        if (n_events == -1) {
            err(EXIT_FAILURE, "poll");
        }

        if (pfds[0].revents & POLLIN) {
            /* We were awoken to read out trace info */
            num_wakes++;
            TDEBUG("Wake");
            TDEBUG("aux_head=  0x%010llu", mmap_header->aux_head);
            TDEBUG("aux_tail=  0x%010llu", mmap_header->aux_tail);
            TDEBUG("aux_offset=0x%010llu", mmap_header->aux_offset);
            TDEBUG("aux_size=  0x%010llu", mmap_header->aux_size);

            read_circular_buf(aux, mmap_header->aux_size,
                mmap_header->aux_head, &mmap_header->aux_tail, out_fd);

            if (pfds[1].fd == -1) {
                /* The tracer was switched off */
                break;
            }
        }

        if (pfds[1].revents & POLLHUP) {
            /* Turn the tracer off after the next buffer read */

            /*
             * XXX can we flush and read now somehow?
             *
             * http://stackoverflow.com/questions/42066651/perf-event-open-is-it-possible-to-explicitly-flush-the-data-aux-mmap-buffers
             */

            TDEBUG("Tracing terminated");
            pfds[1].fd = -1; /* no more events on this fd please */
        }

        if (pfds[0].revents & POLLHUP) {
            /* Parent exited */
            TDEBUG("VM terminated");
            break;
        }
    }
    TDEBUG("Tracing done: awoke %zu times", num_wakes);
}

void
interpreter_loop()
{
    long sum = 4;
    int i, stop_tracer_fd;
    volatile int j;

    VDEBUG("Running interpreter loop...");

    for (i = 0; i < LARGE; i++) {
        /* "JIT Merge Point" */
        if (i == HOT_THRESHOLD) {
            stop_tracer_fd = trace_on();
        }

        for (j = 0; j < 10000; j++) {
            if (j % 2 == 0) {
                sum += i * 2 % 33;
            } else {
                sum += i * 5 % 67;
            }
        }

        if (i == HOT_THRESHOLD) {
            VDEBUG("Tell tracer to stop");
            close(stop_tracer_fd);
        }
    }
    VDEBUG("loop done: %ld", sum);
}


int
trace_on(void)
{
    struct perf_event_attr attr;
    pid_t parent_pid, child_pid;
    int poll_fd, start_tracer_fds[2], stop_tracer_fds[2], res;
    struct perf_event_mmap_page *header;
    void *base, *data, *aux;
    int page_size = getpagesize();
    unsigned char byte;

    VDEBUG("Tracing hot loop");

    /*
     * Processes synchronise via a pipe handshake. In a real VM you would
     * probably use a thread with proper synchronisation primitives.
     */
    if (pipe(start_tracer_fds) != 0) {
        err(EXIT_FAILURE, "pipe");
    }
    if (pipe(stop_tracer_fds) != 0) {
        err(EXIT_FAILURE, "pipe");
    }

    parent_pid = getpid();
    child_pid = fork();

    switch (child_pid) {
    case 0:
        /* Child */
        break;
    case -1:
        /* Error */
        err(EXIT_FAILURE, "fork");
        /* NOREACH*/
        break;
    default:
        /* Parent -- wait for tracer to be ready, then resume */
        close(start_tracer_fds[1]);
        close(stop_tracer_fds[0]);
        VDEBUG("Wait for tracer...");
        for (;;) {
            // XXX this should use poll
            res = read(start_tracer_fds[0], &byte, 1);
            if (res == -1) {
                if (errno == EINTR) {
                    continue;
                } else {
                    err(EXIT_FAILURE, "read");
                }
            } else if (res == 0) { // end of file, i.e. resume
                break;
            } else {
                VDEBUG("Unexpected read result");
                exit(EXIT_FAILURE);
            }
        }
        VDEBUG("Resume under tracer...");
        close(start_tracer_fds[0]);
        return stop_tracer_fds[1];
    }

    /*
     * The tracer (child) now sets up the mmaped DATA and AUX buffers.
     */
    TDEBUG("Tracer initialising");

    // attr.sample_id_all = 1; XXX YYY needed?
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = 7; /* XXX Read from /sys/bus/event_source/devices/intel_pt/type */
    attr.size = sizeof(struct perf_event_attr);

    /* Stuff to exclude */
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1; // hypervisor

    /* Pure magic */
    //attr.config = 50382338;
    //attr.read_format = 4;

    /* Start disabled */
    attr.disabled = 1;

    /* Wake when 25% of AUX buf written */
    //attr.aux_watermark = AUX_PAGES / 4 * getpagesize();

    /* XXX use named syscall function */
    poll_fd = syscall(SYS_perf_event_open, &attr, parent_pid, -1, -1, 0);
    if (poll_fd == -1) {
        err(EXIT_FAILURE, "syscall");
    }

    /* Data buffer is preceeded by one management page, hence +1 */
    base = mmap(NULL, (1 + DATA_PAGES) * page_size, PROT_WRITE,
        MAP_SHARED, poll_fd, 0);
    if (base == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap");
    }

    header = base;
    data = base + header->data_offset;
    (void) data; /* XXX We will need this in the future to detect packet loss */
    header->aux_offset = header->data_offset + header->data_size;
    header->aux_size = AUX_PAGES * page_size;

    /* AUX mapped R/W so as to have a saturating ring buffer */
    aux = mmap(NULL, header->aux_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, poll_fd, header->aux_offset);
    if (aux == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap2");
    }

    /* Open output file */
    int out_fd = open(TRACE_OUTPUT, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd < 0) {
        err(EXIT_FAILURE, "open");
    }

    /* Closing the write end of the pipe tells the VM to resume */
    close(start_tracer_fds[0]);
    close(start_tracer_fds[1]);
    if (ioctl(poll_fd, PERF_EVENT_IOC_ENABLE, 0) < 0)
        err(EXIT_FAILURE, "ioctl to start tracer");
    close(stop_tracer_fds[1]);

    TDEBUG("Tracer initialised");
    poll_loop(poll_fd, out_fd, header, aux, stop_tracer_fds[0]);

    if (ioctl(poll_fd, PERF_EVENT_IOC_DISABLE, 0) < 0)
        err(EXIT_FAILURE, "ioctl to stop tracer");

    close(poll_fd);
    close(out_fd);
    close(stop_tracer_fds[0]);
    exit(EXIT_SUCCESS);
}

int
main(void)
{
    stash_maps();
    interpreter_loop();
    return (EXIT_SUCCESS);
}
