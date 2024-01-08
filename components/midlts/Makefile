CFLAGS=-Iinclude -I/opt/homebrew/include/
BINS=mid_lts mid_signer

all: $(BINS)

mid_lts: mid_lts.o
mid_signer: /opt/homebrew/lib/libmbedtls.a /opt/homebrew/lib/libmbedcrypto.a mid_sign.o

clean:
	rm -f $(BINS)
