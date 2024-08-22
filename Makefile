VERSION = 0.0
NAME = jpte

PREFIX ?= /usr/local
DESTDIR ?=

INCS = -I.
LIBS = -lc -lm -lxcb -lxcb-keysyms -lxcb-render -lxcb-render-util -ltsm -lutil

CC ?= gcc
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Og ${INCS} \
	  -DVERSION=\"${VERSION}\" -DDEBUG -g
#CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -Ofast \
#	-march=native -mtune=native -pipe ${INCS} \
#	-DVERSION=\"${VERSION}\"
LDFLAGS ?= ${LIBS}

SRC = ${NAME}.c
OBJ = ${SRC:.c=.o}

all: ${NAME}

.c.o:
	${CC} -c ${CFLAGS} $< -o $@

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

.PHONY: all clean install uninstall
