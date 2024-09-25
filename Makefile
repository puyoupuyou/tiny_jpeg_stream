CC?=gcc
LD?=ld
CFLAGS  = -Wall -O2 -g -I/mnt/d/wsl/jpeg-turbo-2.1.4
CFLAGS  += -I/mnt/d/wsl/popt-1.19/src
CFLAGS  += -I/mnt/d/wsl/libevent-2.1.12/include
#LDFLAGS = -L/mnt/d/wsl/jpeg-arm/lib/ -L/mnt/d/wsl/jpeg-am/lib/libjpeg.so -ldl
LDFLAGS = -L/mnt/d/wsl/jpeg-turbo-2.1.4 -lturbojpeg -ldl
LDFLAGS += -L/mnt/d/wsl/popt-1.19/src/libs -lpopt
LDFLAGS += -L/mnt/d/wsl/libevent-2.1.12/libs -levent
LDFLAGS +=  -lpthread

all: tjstream

tjstream: tjstream.o
	$(CC) -o $@ $^ $(LDFLAGS)

tjstream.o: tjstream.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f tjstream tjstream.o

.PHONY: all clean
