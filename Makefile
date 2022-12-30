# build script for xautocfg
#
# sorry, this is not cmake yet.

CXXFLAGS ?= -O3 -march=native

BUILDFLAGS = -std=c++20 -Wall -Wextra -pedantic
LIBS = -lX11 -lXi

all: xautocfg

xautocfg: xautocfg.o
	${CXX} ${BUILDFLAGS} ${CXXFLAGS} ${LIBS} $^ -o $@

%.o: %.cpp
	${CXX} ${BUILDFLAGS} ${CXXFLAGS} -c $^ -o $@


.PHONY: clean
clean:
	rm -f xautocfg
