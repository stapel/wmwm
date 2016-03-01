###########################################################
VERSION = $(shell date +%Y%m%d)

###########################################################
CC = gcc
LD = gcc

PREFIX ?= /usr

BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/man

WARNINGS = -Wall -Wextra -pedantic -Wno-variadic-macros
CFLAGS   = -std=c11 -O2 -pedantic $(WARNINGS) $(EXTRA_CFLAGS)
LDFLAGS  = $(EXTRA_LDFLAGS)

###########################################################
.SUFFIXES: .c .h .o
.PHONY: all depend force clean install uninstall dist

###########################################################
#SRC=$(wildcard *.c)
SRC  = wmwm.c hidden.c list.c
OBJ  = $(SRC:%.c=%.o)

wmwmLIBS = "xcb xcb-ewmh xcb-randr xcb-keysyms xcb-icccm xcb-util xcb-shape"
hiddenLIBS = "xcb xcb-ewmh xcb-icccm"

DIST = wmwm-$(VERSION)
DISTFILES = $(SRC) Makefile Makefile.dep LICENSE *.h *.man
# XXX get h files from Makefile.dep ?

###########################################################

BINS=wmwm hidden

all: $(OBJ) $(BINS) | Makefile.dep

wmwm: wmwm.o list.o
hidden: hidden.o

$(BINS):
	$(LD) $(LDFLAGS) $^ $(shell pkg-config $($@LIBS) --libs) -o $@

depend: $(SRC)
	@rm -f Makefile.dep || true
	@for file in $(SRC); do \
	echo "get dependencies from $$file";\
	$(CC) $(CFLAGS) -MM $$file >> Makefile.dep || exit 1;\
	echo -e "\t\044(CC) \044(CFLAGS) $(INCLUDE) -o \044@ -c \044<\n" >> Makefile.dep; \
	done

clean:
	rm -f $(OBJ) $(BINS)

install: $(TARGETS)
	install -D -m 755 wmwm $(DESTDIR)$(BINDIR)/wmwm
	install -D -m 644 wmwm.man $(DESTDIR)$(MANDIR)/man1/wmwm.1
	install -D -m 755 hidden $(DESTDIR)$(BINDIR)/hidden
	install -D -m 644 hidden.man $(DESTDIR)$(MANDIR)/man1/hidden.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/wmwm
	rm -f $(DESTDIR)$(MANDIR)/man1/wmwm.1
	rm -f $(DESTDIR)$(BINDIR)/hidden
	rm -f $(DESTDIR)$(MANDIR)/man1/hidden.1


dist: $(DIST).tar.xz

$(DIST).tar.xz:
	mkdir $(DIST)
	cp $(DISTFILES) $(DIST)/
	tar cvf $(DIST).tar $(DIST)
	xz -v $(DIST).tar
	rm -fR $(DIST)

# if this file is corrupt and make would not run, delete it
-include Makefile.dep
