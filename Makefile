CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11
LDFLAGS  = -lncursesw -lpthread

SRCDIR   = src
SRC      = $(SRCDIR)/main.c    \
           $(SRCDIR)/ui.c      \
           $(SRCDIR)/dialogs.c \
           $(SRCDIR)/viewer.c  \
           $(SRCDIR)/theme.c   \
           $(SRCDIR)/fs.c      \
           $(SRCDIR)/scan.c    \
           $(SRCDIR)/cache.c

OBJ      = $(SRC:.c=.o)
BIN      = fmanager

.PHONY: all clean run rebuild

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
