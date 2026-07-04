CC = gcc
CFLAGS = -I./include $(shell pkg-config --cflags libdpdk)
LDFLAGS = $(shell pkg-config --libs libdpdk)

TARGET = bin/main
SRC = $(wildcard src/*.c)

all:
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET)

run: clean all
	sudo ./$(TARGET) -l 0-4 --vdev 'net_pcap0,rx_pcap=/test/traffic.pcap' --no-pci

