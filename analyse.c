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
    //struct pt_pkt_decoder *decoder;
    struct pt_config config;
    int errcode;
    //struct pt_packet packet;

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.begin = tbuf;
    config.end = tbuf + tbuf_len;

    errcode = pt_cpu_read(&config.cpu);
    if (errcode < 0) {
        err(EXIT_FAILURE, "pt_cpu_read");
    }

#if 0
    decoder = pt_pkt_alloc_decoder(&config);
    if (!decoder)
        err(EXIT_FAILURE, "decoder");

    errcode = pt_pkt_sync_forward(decoder);
    if (errcode < 0)
        err(EXIT_FAILURE, "sync");

    errcode = pt_pkt_next(decoder, &packet, sizeof(packet));
    if (errcode < 0)
        err(EXIT_FAILURE, "next"); // XXX may just be the end

    pt_pkt_free_decoder(decoder);
#endif
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

    /* Figure out the size of the file */
    if (stat(TRACE_OUTPUT, &st) < 0) {
        err(EXIT_FAILURE, "mmap");
    }

    map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
    if (map == NULL) {
        err(EXIT_FAILURE, "mmap");
    }

    decode(map, st.st_size);

    /* map the file into memory */
    return (EXIT_SUCCESS);
}
