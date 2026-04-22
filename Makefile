CC = clang
CFLAGS = -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS = -shared

GO = go
GOFLAGS = -ldflags "-s -w"

.PHONY: all daemon cli ebpf kernel clean install

all: daemon cli kernel

daemon:
	mkdir -p bin
	cd daemon && $(GO) build $(GOFLAGS) -o ../bin/uroamd

cli:
	mkdir -p bin
	cd cli && $(GO) build $(GOFLAGS) -o ../bin/uroamctl

kernel:
	cd kernel && make

ebpf:
	cd ebpf && clang -target bpf -O2 -g -Iinclude -c uroam.bpf.c -o ../bin/uroam.bpf.o

clean:
	rm -rf bin/*
	cd daemon && $(GO) clean
	cd cli && $(GO) clean
	cd kernel && make clean

install:
	install -D bin/uroamd /usr/local/bin/uroamd
	install -D bin/uroamctl /usr/local/bin/uroamctl
	install -D include/uroam.h /usr/local/include/uroam.h
	ldconfig
