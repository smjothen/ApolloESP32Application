CFLAGS=-Ipb -Wall -fsanitize=address #-pedantic
LDFLAGS=-Ipb -fsanitize=address

all: mid_lts mid_lts

mid_lts: mid_lts.o mid_lts_stress.o mid_lts_format.o

clean:
	rm -f mid_lts flash_lts mid_lts_stress *.o *.pb.h *.pb.c *.ms
