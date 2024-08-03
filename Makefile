VERSION = 0.0
NAME = jpte

PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin

INCS = -I. -I${PREFIX}/include
LIBS = -lc -lm -lxcb -lxcb-keysyms -lxcb-render -lxcb-render-util -ltsm -lutil

CC ?= gcc
### Debug
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Og ${INCS} -DVERSION=\"${VERSION}\" -DDEBUG -g
### Fast compilation speed
#CFLAGS ?= -std=c99 -pipe ${INCS} -DVERSION=\"${VERSION}\"
### Generic
#CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Os ${INCS} -DVERSION=\"${VERSION}\"
### Performance
#CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Ofast -march=native -mtune=native -pipe ${INCS} -DVERSION=\"${VERSION}\"
LDFLAGS ?= ${LIBS}

EXEC = ${NAME}

SRC = ${NAME}.c
OBJ = ${SRC:.c=.o}

all: ${NAME}

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h

config.h:
	cp config.def.h $@

${NAME}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -fv ${NAME} ${OBJ}

install: all
	install -Dm755 ${NAME} ${DESTDIR}${PREFIX}/bin/${NAME}

uninstall:
	rm -fv ${DESTDIR}${PREFIX}/bin/${NAME}

.PHONE: all clean install uninstall
