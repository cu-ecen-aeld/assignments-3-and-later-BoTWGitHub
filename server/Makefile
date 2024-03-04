SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CC ?= gcc
LDFLAGS ?= -lpthread -lrt
CFLAGS=-g -Wall -Werror

all:
	$(CC) $(CFLAGS) -c -o $(OBJS) $(SRC)
	$(CC) $(CFLAGS) -I/ $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map