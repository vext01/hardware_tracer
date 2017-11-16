/*
 * Dummy VM
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/mman.h>
#include <poll.h>
#include <inttypes.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <semaphore.h>

#define LARGE 15000
#define HOT_THRESHOLD 1027
#define TRACE_OUTPUT "trace.data"

/* Debug helpers */
#define DPRINTF(who, x...)                                      \
    do {                                                        \
        fprintf(stderr, "[%s](%s:%s:%d): ",              \
            who, __func__, __FILE__, __LINE__);   \
        fprintf(stderr, x);                                 \
        fprintf(stderr, "\n");                              \
    } while (0)
#define TDEBUG(x...)    DPRINTF("TRACER", x);
#define VDEBUG(x...)    DPRINTF("VM", x);

/* mmaped buffer sizes (in pages). Must be powers of 2! */
#define DATA_PAGES 1024
#define AUX_PAGES 16

struct tracer_ctx {
    /* tracer thread itself */
    pthread_t           tracer_thread;
    /* thread synchronisation stuff */
    sem_t               tracer_init_sem;
    /* pipe used to tell the poll loop that the interp loop is finished */
    int                 loop_done_fds[2];
    /* fd used to talk to the perf event layer */
    int                 perf_fd;
    /* PID of the interpreter loop */
    pid_t               interp_pid;
    /* PT-related stuff */
    int                         out_fd;         /* Trace written here */
    struct perf_event_mmap_page *mmap_header;   /* Info about the aux buffer */
    void                        *aux;           /* Aux buffer itself */
};

/* Protos */
void trace_on(struct tracer_ctx *);
void interpreter_loop(void);
void poll_loop(struct tracer_ctx *);
void read_circular_buf(void *, __u64, __u64, __u64 *, int);
void write_buf_to_disk(int, void *, __u64);
void stash_maps(void);
void *do_tracer(void *);

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
    __u64 head = head_monotonic;

    head = head % size; /* head must be manually wrapped */

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
poll_loop(struct tracer_ctx *tr_ctx)
{
    void *aux = tr_ctx->aux;
    __u64 head;
    struct perf_event_mmap_page *mmap_header = tr_ctx->mmap_header;
    struct pollfd pfds[2] = {
        {tr_ctx->perf_fd, POLLIN | POLLHUP, 0},
        {tr_ctx->loop_done_fds[0], POLLHUP, 0}
    };
    int n_events = 0;
    size_t num_wakes = 0;

    while (1) {
        n_events = poll(pfds, 2, INFTIM);
        if (n_events == -1) {
            err(EXIT_FAILURE, "poll");
        }

        if ((pfds[0].revents & POLLIN) || (pfds[1].revents & POLLHUP)) {
            /* See <linux/perf_event.h> for why we need the asm block */
            head = mmap_header->aux_head;
            asm volatile ("" : : : "memory");

            /* We were awoken to read out trace info, or we tracing stopped */
            num_wakes++;
            TDEBUG("Wake");
            TDEBUG("aux_head=  0x%010llu", head);
            TDEBUG("aux_tail=  0x%010llu", mmap_header->aux_tail);
            TDEBUG("aux_offset=0x%010llu", mmap_header->aux_offset);
            TDEBUG("aux_size=  0x%010llu", mmap_header->aux_size);

            read_circular_buf(aux, mmap_header->aux_size,
                head, &mmap_header->aux_tail, tr_ctx->out_fd);

            if (pfds[1].revents & POLLHUP) {
                break;
            }
            //pfds[1].fd = -1;
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
    int i;
    struct tracer_ctx tr_ctx;
    pid_t p = getpid();

    memset(&tr_ctx, 0, sizeof(tr_ctx));
    tr_ctx.interp_pid = getpid();

    VDEBUG("Running interpreter loop...");

    for (i = 0; i < LARGE; i++) {
        /* "JIT Merge Point" */
        if (i == HOT_THRESHOLD) {
            VDEBUG("Hot loop start");
            trace_on(&tr_ctx);
            sleep(1);
        }

        for (int j = 0; j < 1000; j++) {
            /* Start Marker*/
            asm volatile(
                "nop\n\t"
                "nop\n\t"
                "nop\n\t"
                :::);

            if (i % getpid() == 0) {
                sum += i * p % 33;
            } else {
                sum += i * 5 % 67 + p;
            }

            /* Stop Marker*/
            asm volatile(
                "nop\n\t"
                "nop\n\t"
                :::);
        }

        if (i == HOT_THRESHOLD) {
            VDEBUG("Hot loop end");
            if (ioctl(tr_ctx.perf_fd, PERF_EVENT_IOC_DISABLE, 0) < 0)
                err(EXIT_FAILURE, "ioctl to stop tracer");

            if (close(tr_ctx.loop_done_fds[1]) == -1) {
                err(EXIT_FAILURE, "close");
            }
            TDEBUG("Turned off tracer");
        }
    }
    VDEBUG("Waiting for tracer thread to terminate");
    if (pthread_join(tr_ctx.tracer_thread, NULL) != 0) {
        err(EXIT_FAILURE, "pthread_join");
    }
    VDEBUG("loop done: %ld", sum);
}

void
trace_on(struct tracer_ctx *tr_ctx)
{
    int rc;

    /* pipe used for the VM to flag the user loop is complete */
    if (pipe(tr_ctx->loop_done_fds) != 0) {
        err(EXIT_FAILURE, "pipe");
    }

    /* Use a semaphore to wait for the tracer to be ready */
    rc = sem_init(&tr_ctx->tracer_init_sem, 0, 0);
    if (rc == -1) {
        err(EXIT_FAILURE, "sem_init");
    }

    rc = pthread_create(&tr_ctx->tracer_thread, NULL, do_tracer, (void *) tr_ctx);
    if (rc) {
        err(EXIT_FAILURE, "pthread_create");
    }

    VDEBUG("Wait for tracer to be ready...");
    sem_wait(&tr_ctx->tracer_init_sem);
    VDEBUG("Resuming interpreter loop");
}

/*
 * Set up Intel PT buffers and start a poll() loop for reading out the trace
 */
void *
do_tracer(void *arg)
{
    struct perf_event_attr attr;
    struct perf_event_mmap_page *header;
    void *base, *data, *aux;
    int page_size = getpagesize();
    struct tracer_ctx *tr_ctx = (struct tracer_ctx *) arg;

    TDEBUG("Tracer initialising");

    /* Basic configuration */
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = 7; /* XXX Read from /sys/bus/event_source/devices/intel_pt/type */
    attr.size = sizeof(struct perf_event_attr);

    /* Exclude the kernel */
    attr.exclude_kernel = 1;

    /* Exclude the hypervisor */
    attr.exclude_hv = 1;

    /* Start disabled */
    attr.disabled = 1;

    /* No skid */
    attr.precise_ip = 3;

    /* Acquire file descriptor through which to talk to Intel PT */
    tr_ctx->perf_fd = syscall(SYS_perf_event_open, &attr, tr_ctx->interp_pid, -1, -1, 0);
    if (tr_ctx->perf_fd == -1) {
        err(EXIT_FAILURE, "syscall");
    }

    /*
     * Allocate buffers
     *
     * Data buffer is preceeded by one management page, hence `1 + DATA_PAGES'
     */
    base = mmap(NULL, (1 + DATA_PAGES) * page_size, PROT_WRITE,
        MAP_SHARED, tr_ctx->perf_fd, 0);
    if (base == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap");
    }

    header = base;
    data = base + header->data_offset;
    (void) data; /* XXX We will need this in the future to detect packet loss */
    header->aux_offset = header->data_offset + header->data_size;
    header->aux_size = AUX_PAGES * page_size;
    tr_ctx->mmap_header = header;

    /* AUX mapped R/W so as to have a saturating ring buffer */
    aux = mmap(NULL, header->aux_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, tr_ctx->perf_fd, header->aux_offset);
    if (aux == MAP_FAILED) {
        err(EXIT_FAILURE, "mmap2");
    }
    tr_ctx->aux = aux;

    /* Open output file */
    tr_ctx->out_fd = open(TRACE_OUTPUT, O_WRONLY | O_CREAT | O_TRUNC);
    if (tr_ctx->out_fd < 0) {
        err(EXIT_FAILURE, "open");
    }

    /* Turn on Intel PT */
    if (ioctl(tr_ctx->perf_fd, PERF_EVENT_IOC_RESET, 0) < 0)
        err(EXIT_FAILURE, "ioctl to start tracer");
    if (ioctl(tr_ctx->perf_fd, PERF_EVENT_IOC_ENABLE, 0) < 0)
        err(EXIT_FAILURE, "ioctl to start tracer");

    /* Resume the interpreter loop */
    TDEBUG("Signalling the VM to continue");
    sem_post(&tr_ctx->tracer_init_sem);

    /* Start reading out of the aux buffer */
    poll_loop(tr_ctx);

    /* Clean up an terminate thread */
    close(tr_ctx->perf_fd);
    close(tr_ctx->out_fd);

    VDEBUG("Tracer thread exiting");
    pthread_exit(NULL);
}

int
main(void)
{
    stash_maps();
    interpreter_loop();
    return (EXIT_SUCCESS);
}
