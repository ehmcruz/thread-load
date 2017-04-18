CC=gcc
CPP=g++
AR=ar
CFLAGS=-O2 -fPIC -ggdb -Wall
CPPFLAGS=$(CFLAGS)
LDFLAGS=-lpthread -shared -Wl,--no-as-needed -ldl

csrcfiles=connect.c
cppsrcfiles=
headerfiles=$(wildcard *.h)

libnamedyn=libtload.so

#################################################################

objfiles=$(patsubst %.c,%.o,$(csrcfiles)) $(patsubst %.cpp,%.o,$(cppsrcfiles))

%.o: %.c $(headerfiles)
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cpp $(headerfiles)
	$(CPP) -c $(CPPFLAGS) $< -o $@

#################################################################

all: $(libnamedyn)
	@echo "Compiled! Yes!"

$(libnamedyn): $(objfiles)
	$(CC) $(objfiles) -o $(libnamedyn) $(CFLAGS) $(LDFLAGS)

clean:
	- rm -f *.o
	- rm -f $(libnamedyn)

