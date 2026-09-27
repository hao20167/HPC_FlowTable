CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra -Wformat=2 -Werror=format -I./include $(shell pkg-config --cflags libdpdk) -pthread
LDFLAGS = $(shell pkg-config --libs libdpdk) -pthread

TARGET = bin/sieucapvip
SRC = $(wildcard src/*.c)

all:
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET)

run: clean all
	# sudo ./$(TARGET) -l 0-7 --vdev 'net_pcap0,rx_pcap=tests/pcap/test.pcap,rx_pcap=tests/pcap/test.pcap,rx_pcap=tests/pcap/test.pcap,rx_pcap=tests/pcap/test.pcap,infinite_rx=1' --no-pci
	sudo ./$(TARGET) -l 0-7 --vdev 'net_pcap0,rx_pcap=tests/pcap/smallFlows.pcap,rx_pcap=tests/pcap/smallFlows.pcap,rx_pcap=tests/pcap/smallFlows.pcap,rx_pcap=tests/pcap/smallFlows.pcap,infinite_rx=1' --no-pci
	# sudo ./$(TARGET) -l 0-7 --vdev 'net_pcap0,rx_pcap=/dev/shm/smallFlows.pcap,rx_pcap=/dev/shm/smallFlows.pcap,rx_pcap=/dev/shm/smallFlows.pcap,rx_pcap=/dev/shm/smallFlows.pcap,infinite_rx=1' --no-pci
