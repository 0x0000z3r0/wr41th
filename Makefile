DEST := build
KDIR ?= $(firstword $(wildcard /lib/modules/*/build))

.PHONY: all kmod r2 core demo util in-all in-kmod in-core in-r2 in-demo in-util

all kmod r2 core demo util:
	@./docker/run.sh $@

in-all: in-kmod in-core in-r2 in-demo in-util

in-demo:
	$(MAKE) -C demo DEST=$(CURDIR)/$(DEST)/demo

in-core:
	mkdir -p $(DEST)/core
	gcc -c -Icore -O2 -Wall -Wextra -fPIC core/io.c -o $(DEST)/core/io.o
	ar rcs $(DEST)/core/libwr.a $(DEST)/core/io.o

in-util: in-core
	mkdir -p $(DEST)/util
	gcc -Icore -O2 -Wall -Wextra -o $(DEST)/util/wr41th util/main.c $(DEST)/core/io.o

in-kmod:
	@if [ -z "$(KDIR)" ] || [ ! -d "$(KDIR)" ]; then echo "kmod: no KDIR"; exit 1; fi
	mkdir -p $(DEST)/driver
	$(MAKE) -C $(KDIR) M=$(CURDIR)/driver modules
	cp -f driver/wr41th.ko $(DEST)/driver/
	$(MAKE) -C $(KDIR) M=$(CURDIR)/driver clean

in-r2: in-core
	@if ! pkg-config --exists r_core; then echo "r2: no r_core"; exit 1; fi
	mkdir -p $(DEST)/radare2
	libdir="$${R2LIBDIR:-$$(pkg-config --variable=libdir r_core)}"; \
	gcc -shared -fPIC -Icore $$(pkg-config --cflags r_core) \
		radare2/plug.c $(DEST)/core/io.o \
		$$(pkg-config --libs r_core) -Wl,-rpath,$$libdir \
		-o $(DEST)/radare2/wr41th.so; \
	echo "r2: rpath=$$libdir"
