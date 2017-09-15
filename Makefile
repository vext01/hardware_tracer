CC = gcc
CFLAGS += -Wall -Wextra -g -I${PT_PRIVATE_INC}
IPT_INC = deps/inst/libipt/include

# Borrowing a private helper function from libipt
PT_PRIVATE_INC = deps/src/processor-trace/libipt/internal/include
PT_CPU_SRC = deps/src/processor-trace/libipt/src
PT_CPUID_SRC = deps/src/processor-trace/libipt/src/posix

.PHONY: deps pt_cpu.c

all: deps vm analyse

deps:
	cd deps && ${MAKE}

clean:
	rm -f vm trace.data trace.dec dis.sh maps pt_cpu.o pt_cpuid.o analyse

pt_cpu.o: ${PT_CPU_SRC}/pt_cpu.c
	${CC} -c -I${IPT_INC} ${CFLAGS} $< -o $@

pt_cpuid.o: ${PT_CPUID_SRC}/pt_cpuid.c
	${CC} -c ${CFLAGS} $< -o $@

analyse: analyse.c pt_cpu.o pt_cpuid.o
	${CC} ${CFLAGS} -I${IPT_INC} ${LDFLAGS} $^ -o $@

run: vm
	sudo ./vm

	@echo "decoding trace..."
	sudo ./deps/inst/libipt/bin/ptdump trace.data > trace.dec

	@echo "make disasemble script"
	base=`head -1  maps | awk '{sub(/-.*/, "", $$1); print "0x" $$1}'` && \
	     echo "#!/bin/sh\nr2 -B $${base} vm" > dis.sh
