CFLAGS+=	`ncurses6-config --cflags`
CFLAGS+=	-I`cdk5-config --prefix`/include

CFLAGS+=	-Wall

LIBS+=		`cdk5-config --libs`
LIBS+=		`ncurses6-config --libs`

all: cmixer

cmixer: cmixer.o
	$(CC) $(LDFLAGS) cmixer.o $(LIBS) -o cmixer

cmixer.o:
	$(CC) $(CFLAGS) -c cmixer.c -o cmixer.o

clean:
	rm -f *.o cmixer
