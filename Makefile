CXX:=g++
CXXFLAGS:=-O3

LIBS=-lX11 -lXi

xautocfg: xautocfg.cpp
	${CXX} -std=c++20 -Wall -Wextra -pedantic ${CXXFLAGS} ${LIBS} $^ -o $@
