CC = gcc
CFLAGS += -Wall -Wextra -g

.PHONY: deps

all: deps vm

deps:
	cd deps && ${MAKE}

clean:
	rm -f vm trace.data trace.dec dis.sh maps

run: vm
	sudo ./vm

	@echo "decoding trace..."
	sudo ./deps/inst/libipt/bin/ptdump trace.data > trace.dec

	@echo "make disasemble script"
	base=`head -1  maps | awk '{sub(/-.*/, "", $$1); print "0x" $$1}'` && \
	     echo "#!/bin/sh\nr2 -B $${base} vm" > dis.sh
