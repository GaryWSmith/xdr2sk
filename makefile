xdr_to_sk: xdr_to_sk_v6.c tinyexpr.c
	gcc -o xdr2sk xdr_to_sk_v6.c tinyexpr.c -lm -lpthread -lconfig
