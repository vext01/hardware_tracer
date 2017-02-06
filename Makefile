#CC = clang
CC = gcc
CFLAGS += -Wall -Wextra -g
all: vm

clean:
	rm -f vm
