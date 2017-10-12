/*
 * Analyse packets dumped to disk
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
#include <sys/stat.h>

#include <limits.h>
#include <linux/perf_event.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/ioctl.h>
#include <intel-pt.h>
#include <pt_cpu.h>

#define TRACE_OUTPUT "trace.data"

int
decode(unsigned char *tbuf, off_t tbuf_len) {
    struct pt_packet_decoder *decoder;
    struct pt_config config;
    int errcode;
    struct pt_packet packet;
    u_int64_t pkts_processed = 0;

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.begin = tbuf;
    config.end = tbuf + tbuf_len;

    errcode = pt_cpu_read(&config.cpu);
    if (errcode < 0) {
        err(EXIT_FAILURE, "pt_cpu_read");
    }

    decoder = pt_pkt_alloc_decoder(&config);
    if (!decoder)
        err(EXIT_FAILURE, "decoder");

    errcode = pt_pkt_sync_forward(decoder);
    if (errcode < 0)
        err(EXIT_FAILURE, "sync");

    for (;;) {
        errcode = pt_pkt_next(decoder, &packet, sizeof(packet));
        if (errcode < 0) {
            if (errcode == -pte_eos) {
                break;
            } else {
                err(EXIT_FAILURE, "pt_pkt_next");
            }
        }
        pkts_processed++;
    }

    printf("processed %" PRIu64 " packets\n", pkts_processed);
    pt_pkt_free_decoder(decoder);

    return 0;
}

int
main(void)
{
    void        *map;
    struct      stat st;
    int         fd;

    fd = open(TRACE_OUTPUT, O_RDONLY);
    if (fd < 0) {
        err(EXIT_FAILURE, "open");
    }

    /* Map the trace file into virtual address space */
    if (stat(TRACE_OUTPUT, &st) < 0) {
        err(EXIT_FAILURE, "mmap");
    }

    map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
    if (map == NULL) {
        err(EXIT_FAILURE, "mmap");
    }

    decode(map, st.st_size);

    return EXIT_SUCCESS;
}
