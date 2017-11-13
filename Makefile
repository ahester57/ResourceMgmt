
CC= gcc
CFLAGS= -Wall -g
LDLIBS= -pthread
COMMONSRCS= sighandler.c ipchelper.c deadlock.c
OSSSRCS = oss.c
USERSRCS = user.c
# or perhaps SOURCES= $(wildcard *.c)

OBJECTS= $(patsubst %.c,%.o,$(COMMONSRCS))

.PHONY: all clean

all: oss user 

oss: $(OBJECTS)

user: $(OBJECTS)

$(OBJECTS): sighandler.h ipchelper.h osstypes.h deadlock.h

clean:
	$(RM) $(OBJECTS) *.o *log oss user
