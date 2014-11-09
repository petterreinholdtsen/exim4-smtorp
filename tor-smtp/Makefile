CFLAGS = -Wall

all: tor-smtp strip

tor-smtp: atomicio.o tor-smtp.o

strip:
	strip tor-smtp

clean:
	rm -f *.o tor-smtp *~

tar:
	(cd ..; tar czf tor-smtp/tor-smtp.tar.gz tor-smtp/*.{c,h} tor-smtp/Makefile)

