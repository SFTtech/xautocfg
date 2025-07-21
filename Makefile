# build script for xautocfg
#
# sorry, this is not cmake yet.

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

CXXFLAGS ?= -O3 -march=native

BUILDFLAGS = -std=c++20 -Wall -Wextra -pedantic
LIBS = -lX11 -lXi

.PHONY: all
all: xautocfg

xautocfg: xautocfg.o
	${CXX} ${BUILDFLAGS} ${CXXFLAGS} $^ ${LIBS} -o $@

%.o: %.cpp
	${CXX} ${BUILDFLAGS} ${CXXFLAGS} -c $^ -o $@

.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/man1
	install -m 0755 xautocfg $(DESTDIR)$(PREFIX)/bin
	install -m 0644 xautocfg.1 $(DESTDIR)$(MANPREFIX)/man1/

.PHONY: clean
clean:
	rm -f xautocfg
