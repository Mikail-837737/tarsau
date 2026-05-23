CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pedantic -O2
TARGET = tarsau
OBJS = main.o tarsau.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

main.o: main.c tarsau.h
	$(CC) $(CFLAGS) -c main.c

tarsau.o: tarsau.c tarsau.h
	$(CC) $(CFLAGS) -c tarsau.c

clean:
	rm -f $(TARGET) $(OBJS)
