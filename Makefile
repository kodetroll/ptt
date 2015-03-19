#
# Makefile for ptt V1.3
# Ver: 1.3
# (C) 2009-2014 KB4OID Labs, a division of Kodetroll Heavy Industries
#
# type 'make' to build
#
CC=gcc
CFLAGS=-O1

all: ptt 

ptt:
	$(CC) $(CFLAGS) -o ptt ptt.c

.PHONY: clean

clean:
	rm -rf *.o ptt	

