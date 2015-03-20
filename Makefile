#
# Makefile for ptt V1.4
# Ver: 1.3
# (C) 2009-2015 KB4OID Labs, a division of Kodetroll Heavy Industries
#
# type 'make' to build
#
CC=gcc
CFLAGS=-O1
LDFLAGS=
LIBS=
DEPS=
OBJS=ptt.o ini.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: ptt 

ptt:	$(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS) $(LDFLAGS)

.PHONY: clean

clean:
	rm -rf *.o

cleanall:
	rm -rf *.o ptt	

install:
	install ptt /usr/local/sbin