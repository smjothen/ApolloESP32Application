CFLAGS=-Iinclude -I../utz/include/ -g -Wall -fsanitize=address #-pedantic
LDFLAGS=-Iinclude -I../utz/include/ -g -fsanitize=address

all: mid_lts mid_lts

mid_lts: mid_lts.o mid_lts_stress.o mid_lts_format.o ../utz/utz.o ../utz/zones.c

clean:
	rm -f mid_lts flash_lts mid_lts_stress *.o *.pb.h *.pb.c *.ms
