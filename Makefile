CC=g++

.cpp.o:
	$(CC) -c $< -o $@

.c.o:
	$(CC) -c $< -o $@

all: rxdec

Decoder.o: Decoder.cpp Decoder.h

rxdec.o: rxdec.cpp Decoder.h

rxdec: rxdec.o Decoder.o
	$(CC) -o $@ $@.o Decoder.o -lwiringPi -lpthread -lpaho-mqttpp3 -lconfig++

clean:
	rm -f *.o core *~ 

mrproper: clean
	rm -f rxdec
