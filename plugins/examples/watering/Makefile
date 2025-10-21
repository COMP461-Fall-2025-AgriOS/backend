CC = g++
CFLAGS = -I../../.. -fPIC -Wall -O2 -std=c++17
LDFLAGS = -shared

TARGET = libwatering_example.so
SRCS = watering_plugin.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) *.o
