CXX = g++
CPPFLAGS = -Isrc
CXXFLAGS = -Wall -pedantic -std=gnu++11 -O2
LIBFLAGS = -Llib -ljank -lreadline
TARGETS = lib/libjank.a bin/jank
INSTALL_PATH = /usr/local
SOURCES = src/jank.cc
OBJECTS = src/jank.o

.PHONY: all clean install test library

all: $(TARGETS)

clean:
	rm -f $(TARGETS)
	rm -f src/*.o
	rm -fr bin
	rm -fr lib

install:
	install -m 644 lib/libjank.a $(INSTALL_PATH)/lib
	install -m 644 src/jank.hh $(INSTALL_PATH)/include
	install -m 755 bin/jank $(INSTALL_PATH)/bin

test: $(TARGETS)
	./bin/jank -vt

library: lib/libjank.a

lib/libjank.a: $(OBJECTS)
	if [ ! -d lib ]; then mkdir -vp lib; fi
	ar crfv $@ $^ 

bin/jank: src/main.o lib/libjank.a
	if [ ! -d bin ]; then mkdir -vp bin; fi
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBFLAGS)

src/main.cc: src/jank.hh
