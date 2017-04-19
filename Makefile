CC=gcc
CPP=g++
AR=ar
CFLAGS=-O2 -fPIC -ggdb -Wall
CPPFLAGS=$(CFLAGS)
LDFLAGS=-lpthread -shared -Wl,--no-as-needed -ldl

headerfiles=$(wildcard *.h)

libnamedyn=libtload.so
libnamedynpapi=libtloadpapi.so

#################################################################

all: $(libnamedyn)
	@echo "Compiled! Yes!"

papi: $(libnamedyn) $(libnamedynpapi)
	@echo "Compiled! Yes!"

#######################

connect.o: connect.c $(headerfiles)
	$(CC) -c connect.c -o connect.o $(CFLAGS)

$(libnamedyn): connect.o
	$(CC) connect.o -o $(libnamedyn) $(CFLAGS) $(LDFLAGS)

#######################

connect-papi.o: connect.c $(headerfiles)
	$(CC) -c connect.c -o connect-papi.o $(CFLAGS) -DLIBTLOAD_SUPPORT_PAPI

$(libnamedynpapi): connect-papi.o
	$(CC) connect-papi.o -o $(libnamedynpapi) $(CFLAGS) $(LDFLAGS)

#######################

clean:
	- rm -f *.o
	- rm -f $(libnamedyn)
	- rm -f $(libnamedynpapi)

