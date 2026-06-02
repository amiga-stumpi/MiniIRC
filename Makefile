AMIGA_PREFIX ?= /opt/amiga
NETINC ?= /opt/amiga-netinclude/include

CC = $(AMIGA_PREFIX)/bin/m68k-amigaos-gcc
CPPFLAGS = -Iinclude -I$(NETINC)
CFLAGS = -O2 -Wall -Wextra -mcrt=nix13 -DAMITCP13_OS13

MINI_IRC_SRCS = \
	src/mini_irc_gui.c \
	src/mini_irc_session.c
MINI_IRC_OBJS = $(MINI_IRC_SRCS:.c=.o)
MINI_IRC_CLI_SRCS = src/mini_irc_cli.c src/mini_irc_session.c
MINI_IRC_CLI_OBJS = $(MINI_IRC_CLI_SRCS:.c=.o)

all: build/MiniIRC

MiniIRC: build/MiniIRC

mini_irc_cli: build/mini_irc_cli

build/MiniIRC: $(MINI_IRC_OBJS)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(MINI_IRC_OBJS)

build/mini_irc_cli: $(MINI_IRC_CLI_OBJS)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(MINI_IRC_CLI_OBJS)

clean:
	rm -f $(MINI_IRC_OBJS) src/mini_irc_cli.o build/MiniIRC build/mini_irc_cli

.PHONY: all clean MiniIRC mini_irc_cli
