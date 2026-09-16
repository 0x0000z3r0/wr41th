DEST ?= ../build/demo
CC ?= gcc
CFLAGS ?= -O0 -Wall -Wextra
LIBS ?= -ldl
SRCS := $(wildcard *.c)
BINS := $(patsubst %.c,$(DEST)/%,$(SRCS))

.PHONY: all

all: $(BINS)

$(DEST)/%: %.c
	mkdir -p $(DEST)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $<
