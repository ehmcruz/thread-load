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

lib.o: lib.c $(headerfiles)
	$(CC) -c lib.c -o lib.o $(CFLAGS)

connect.o: connect.c $(headerfiles)
	$(CC) -c connect.c -o connect.o $(CFLAGS)

$(libnamedyn): connect.o lib.o
	$(CC) connect.o lib.o -o $(libnamedyn) $(CFLAGS) $(LDFLAGS)

#######################

lib-papi.o: lib.c $(headerfiles)
	$(CC) -c lib.c -o lib-papi.o $(CFLAGS) -DLIBTLOAD_SUPPORT_PAPI

connect-papi.o: connect.c $(headerfiles)
	$(CC) -c connect.c -o connect-papi.o $(CFLAGS) -DLIBTLOAD_SUPPORT_PAPI

papi.o: papi.c $(headerfiles)
	$(CC) -c papi.c -o papi.o $(CFLAGS) -DLIBTLOAD_SUPPORT_PAPI

$(libnamedynpapi): connect-papi.o lib-papi.o papi.o
	$(CC) connect-papi.o lib-papi.o papi.o -o $(libnamedynpapi) $(CFLAGS) $(LDFLAGS) -lpapi

#######################

clean:
	- rm -f *.o
	- rm -f $(libnamedyn)
	- rm -f $(libnamedynpapi)

