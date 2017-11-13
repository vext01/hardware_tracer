CC = gcc
CFLAGS += -Wall -Wextra -g -I${PT_PRIVATE_INC}
IPT_INC = deps/inst/include
IPT_LIB = deps/inst/lib

# Borrowing a private helper function from libipt
PT_PRIVATE_INC = deps/processor-trace/libipt/internal/include
PT_CPU_SRC = deps/processor-trace/libipt/src
PT_CPUID_SRC = deps/processor-trace/libipt/src/posix

.PHONY: deps pt_cpu.c dis-trace r2

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
	${CC} ${CFLAGS} -I${IPT_INC} -Wl,-rpath=${IPT_LIB} -L${IPT_LIB} -lipt ${LDFLAGS} $^ -o $@

vm: vm.c
	${CC} ${CFLAGS} -pthread ${LDFLAGS} $^ -o $@


# get instructions
# ./deps/inst/bin/ptxed -v --cpu auto --pt trace.pt --raw vm:<base-addr> | less

run: vm
	sudo ./vm

	@echo "decoding trace..."
	sudo ./deps/inst/libipt/bin/ptdump trace.data > trace.dec

	@echo "make disasemble script"
	base=`head -1  maps | awk '{sub(/-.*/, "", $$1); print "0x" $$1}'` && \
	     echo "#!/bin/sh\nr2 -B $${base} vm" > dis.sh

dis-trace:
	base=`awk '$$2=="r-xp" && $$6~"vm" {split($$1,flds,"-"); print flds[1]}' maps` && \
		sudo chmod 755 trace.data && \
		./deps/inst/bin/ptxed -v --cpu auto --pt trace.data --raw vm:0x$${base} | less

r2:
	base=`awk '$$2=="r-xp" && $$6~"vm" {split($$1,flds,"-"); print flds[1]}' maps` && \
		sudo chmod 755 trace.data && r2 -B 0x$${base} vm
