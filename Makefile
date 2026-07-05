CC = gcc
CFLAGS = -Wall -Wextra -Wformat=2 -Werror=format -I./include $(shell pkg-config --cflags libdpdk) -pthread
LDFLAGS = $(shell pkg-config --libs libdpdk) -pthread

TARGET = bin/main
SRC = $(wildcard src/*.c)

all:
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET)

run: clean all
	sudo ./$(TARGET) -l 0-4 --vdev 'net_pcap0,rx_pcap=tests/sample.pcap,infinite_rx=1' --no-pci
