CC = gcc
CFLAGS = -I./include $(shell pkg-config --cflags libdpdk)
LDFLAGS = $(shell pkg-config --libs libdpdk)

TARGET = bin/main
SRC = $(wildcard *.c)

all:
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET)

run: clean all
	sudo ./$(TARGET) -l 0-4 --no-pci

