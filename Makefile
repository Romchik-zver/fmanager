CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11
LDFLAGS  = -lncursesw -lpthread

SRCDIR   = src
SRC      = $(SRCDIR)/main.c    \
           $(SRCDIR)/ui.c      \
           $(SRCDIR)/dialogs.c \
           $(SRCDIR)/viewer.c  \
           $(SRCDIR)/theme.c   \
           $(SRCDIR)/opener.c  \
           $(SRCDIR)/fs.c      \
           $(SRCDIR)/scan.c    \
           $(SRCDIR)/cache.c

OBJ      = $(SRC:.c=.o)
BIN      = fmanager
VERSION  = 0.2.0
ARCH     = x86_64

.PHONY: all clean run rebuild install uninstall install-user uninstall-user appimage tbz
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

rebuild: clean all

run: all
	./$(BIN)

PREFIX ?= /usr/local

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

install-user: $(BIN)
	install -Dm755 $(BIN) $(HOME)/.local/bin/$(BIN)

uninstall-user:
	rm -f $(HOME)/.local/bin/$(BIN)

appimage:
	VERSION=$(VERSION) ARCH=$(ARCH) ./build-appimage.sh

tbz:
	VERSION=$(VERSION) ARCH=$(ARCH) ./build-tbz.sh
