VERSION=20160227
DIST=wmwm-$(VERSION)
SRC=wmwm.c list.c config.h list.h hidden.c
DISTFILES=LICENSE Makefile wmwm.man hidden.man $(SRC)

#CC=clang
#debug=0
CC ?= gcc

CFLAGS += -std=c11 -Wall -Wextra -pedantic -O2 -Wno-variadic-macros

ifeq ($(debug),1)
	CFLAGS += -g -DDEBUG -Wno-format-extra-args -O1 -fsanitize=address -fno-omit-frame-pointer -fsanitize=leak -fsanitize=undefined
endif


LDFLAGS += -L/usr/local/lib -lxcb -lxcb-ewmh -lxcb-randr\
		   -lxcb-keysyms -lxcb-icccm -lxcb-util -lxcb-shape

ifeq ($(coverage),1)
	CFLAGS += -coverage
	LDFLAGS += -coverage
endif

RM = /bin/rm
PREFIX ?= /usr/local

TARGETS=wmwm hidden
OBJS=wmwm.o list.o

all: $(TARGETS)

wmwm: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

wmwm-static: $(OBJS)
	$(CC) -o $@ $(OBJS) -static $(CFLAGS) $(LDFLAGS) \
	-lXau -lXdmcp

wmwm.o: wmwm.c list.h config.h Makefile

list.o: list.c list.h Makefile

install: $(TARGETS)
	install -D -m 755 wmwm $(DESTDIR)/$(PREFIX)/bin/wmwm
	install -D -m 644 wmwm.man $(DESTDIR)/$(PREFIX)/man/man1/wmwm.1
	install -D -m 755 hidden $(DESTDIR)/$(PREFIX)/bin/hidden
	install -D -m 644 hidden.man $(DESTDIR)/$(PREFIX)/man/man1/hidden.1

uninstall: deinstall
deinstall:
	$(RM) $(PREFIX)/bin/wmwm
	$(RM) $(PREFIX)/man/man1/wmwm.1
	$(RM) $(PREFIX)/bin/hidden
	$(RM) $(PREFIX)/man/man1/hidden.1

$(DIST).tar.bz2:
	mkdir $(DIST)
	cp -v $(DISTFILES) $(DIST)/
	tar cvf $(DIST).tar --exclude .git $(DIST)
	bzip2 -9 $(DIST).tar
	$(RM) -rf $(DIST)

dist: $(DIST).tar.bz2

clean:
	$(RM) -f $(TARGETS) *.o *.gc* gmon.out *.plist

distclean: clean
	$(RM) -f $(DIST).tar.bz2
