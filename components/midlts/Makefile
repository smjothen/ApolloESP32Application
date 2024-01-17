CFLAGS=-I../nanopb/ -Iinclude -I. -Wall -fsanitize=address #-pedantic
LDFLAGS=-I../nanopb/ -Iinclude -I. -fsanitize=address
NANOPB=nanopb_generator.py

all: mid_lts

mid_session.pb.c: mid_session.proto mid_session.options
	$(NANOPB) -L quote -C mid_session.proto

mid_lts.o: mid_session.pb.o

mid_lts: mid_lts.o mid_lts_format.o mid_session.pb.o ../nanopb/pb_common.o ../nanopb/pb_encode.o ../nanopb/pb_decode.o

clean:
	rm -f mid_lts *.o *.pb.h *.pb.c ../nanopb/*.o *.ms
