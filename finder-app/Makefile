SRC := writer.c
TARGET = writer
OBJS := $(SRC:.c=.o)
CC := $(CROSS_COMPILE)gcc

all:
	$(CC) -g -Wall  -c -o $(OBJS) $(SRC)
	$(CC) -g -Wall -I/ $(OBJS) -o $(TARGET)

clean:
	-rm -f *.o $(TARGET) *.elf *.map