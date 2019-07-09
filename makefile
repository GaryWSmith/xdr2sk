CC=gcc
CFLAGS=-I.

xdr: xdr_to_sk_v6.o tinyexpr.o
	$(CC) -o xdr2sk xdr_to_sk_v6.o tinyexpr.o -lm -lconfig

clean: 
	rm -r -f ./*.o ./xdr2sk /usr/bin/xdr2sk /etc/xdr2sk

install:
	cp xdr2sk /usr/bin/xdr2sk 
	mkdir /etc/xdr2sk
	cp Dictionary.cfg /etc/xdr2sk/
