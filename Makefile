CC = clang
CFLAGS = -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS = -shared

GO = go
GOFLAGS = -ldflags "-s -w"

.PHONY: all daemon lib cli clean install

all: daemon lib cli

daemon:
	cd daemon && $(GO) build $(GOFLAGS) -o ../bin/memopt-daemon

lib: libmemopt.so

libmemopt.so: src/memopt.o src/allocator.o src/pool.o src/numa.o src/ksm.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldl -lnuma

src/%.o: src/%.c include/%.h
	$(CC) $(CFLAGS) -c $< -o $@

cli:
	cd cli && $(GO) build $(GOFLAGS) -o ../bin/memopt

clean:
	rm -f src/*.o libmemopt.so bin/*
	cd daemon && $(GO) clean
	cd cli && $(GO) clean

install:
	install -D bin/memopt-daemon /usr/local/bin/memopt-daemon
	install -D bin/memopt /usr/local/bin/memopt
	install -D libmemopt.so /usr/local/lib/libmemopt.so
	install -D include/memopt.h /usr/local/include/memopt.h
	ldconfig
