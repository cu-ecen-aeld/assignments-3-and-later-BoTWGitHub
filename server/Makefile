SRC := aesdsocket.c
TARGET = aesdsocket
OBJS := $(SRC:.c=.o)
CC := $(CROSS_COMPILE)gcc
CFLAGS=-g -Wall -Werror

all:
	$(CC) $(CFLAGS)  -c -o $(OBJS) $(SRC)
	$(CC) $(CFLAGS) -I/ $(OBJS) -o $(TARGET) -lpthread

clean:
	-rm -f *.o $(TARGET) *.elf *.map