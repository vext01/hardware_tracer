#CC = clang
CC = gcc
CFLAGS += -Wall -Wextra -g
all: vm

clean:
	rm -f vm

run: vm
	sudo ./vm

	@echo "decoding trace..."
	sudo ./deps/inst/ptdump/bin/ptdump trace.data > trace.dec

	@echo "make disasemble script"
	base=`head -1  maps | awk '{sub(/-.*/, "", $$1); print "0x" $$1}'` && \
	     echo "#!/bin/sh\nr2 -B $${base} vm" > dis.sh
