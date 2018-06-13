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
PROJ=ptt
OBJS=$(PROJ).o ini.o
WHAT=$(PROJ)
#WHERE=/usr/local/sbin
WHERE=~/bin

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: clean $(PROJ) 

$(PROJ): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS) $(LDFLAGS)

.PHONY: clean

clean:
	rm -rf *.o

cleanall: clean
	rm -rf $(PROJ)	

install:
	install $(WHAT) $(WHERE)
