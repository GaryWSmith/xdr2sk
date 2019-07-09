CC=gcc
CFLAGS=-I.

xdr: xdr_to_sk_v6.o tinyexpr.o
	$(CC) -o xdr2sk xdr_to_sk_v6.o tinyexpr.o -lm -lconfig

clean: 
	rm -f ./*.o ./xdr2sk /usr/bin/xdr2sk

install:
	cp xdr2sk /usr/bin/xdr2sk
