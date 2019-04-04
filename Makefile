CFLAGS+=	$$(ncurses6-config --cflags)
CFLAGS+=	-I$$(cdk5-config --prefix)/include

CFLAGS+=	-Wall -Wextra -Wpedantic -std=c11

LIBS+=		$$(cdk5-config --libs)
LIBS+=		$$(ncurses6-config --libs)

all: aiomixer

aiomixer: aiomixer.o
	$(CC) $(LDFLAGS) aiomixer.o $(LIBS) -o aiomixer

aiomixer.o:
	$(CC) $(CFLAGS) -c aiomixer.c -o aiomixer.o

clean:
	rm -f *.o aiomixer
