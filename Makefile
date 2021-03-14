NCURSES6_CFLAGS!=	ncurses6-config --cflags
CDK5_PREFIX!=		cdk5-config --prefix

CFLAGS+=		${NCURSES6_CFLAGS}
CFLAGS+=		-I${CDK5_PREFIX}/include

CFLAGS+=		-Wall -Wextra -Wpedantic -std=c11

.if DEBUG
CFLAGS+=		-Og -g
CFLAGS+=		-fsanitize=address -fsanitize=undefined -fsanitize=leak
LDFLAGS+=		-fsanitize=address -fsanitize=undefined -fsanitize=leak
.endif

CDK5_LIBS!=		cdk5-config --libs
NCURSES6_LIBS!=		ncurses6-config --libs

LIBS+=			${CDK5_LIBS} ${NCURSES6_LIBS}

all: aiomixer

aiomixer: aiomixer.o
	$(CC) $(LDFLAGS) aiomixer.o $(LIBS) -o aiomixer

aiomixer.o:
	$(CC) $(CFLAGS) -c aiomixer.c -o aiomixer.o

clean:
	rm -f *.o aiomixer
