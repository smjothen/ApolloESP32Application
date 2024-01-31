CFLAGS=-Iinclude -I../utz/include/ -I/opt/homebrew/include/ -g -Wall -fsanitize=address #-pedantic
LDFLAGS=-Iinclude -I../utz/include/ -g -fsanitize=address

all: mid_lts mid_signer

mid_lts: mid_lts.o mid_lts_stress.o mid_lts_format.o mid_active.o ../utz/utz.o ../utz/zones.c

mid_signer: mid_sign.o mid_signer.o /opt/homebrew/lib/libmbedtls.a /opt/homebrew/lib/libmbedcrypto.a

clean:
	rm -f mid_lts mid_signer midflash_lts mid_lts_stress *.o *.pb.h *.pb.c *.ms
