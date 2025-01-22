PREFIX ?= /usr/local

all: wg-keepalive.bin

wg-keepalive.bin: wg-keepalive.cpp
	g++ -std=c++20 -o $@ $^ -liniparser -lspdlog -lfmt

clean:
	rm -f *.bin *.o

.PHONY: all clean

install:
	install -Dm755 wg-keepalive.bin $(DESTDIR)$(PREFIX)/bin/wg-keepalive
	install -Dm644 wg-keepalive@.service $(DESTDIR)$(PREFIX)/lib/systemd/system/wg-keepalive@.service
