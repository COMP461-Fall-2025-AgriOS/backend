CC = g++
CFLAGS = -Iinclude -I.. -I../.. -I../internal-representations/include -I../modules/include -fPIC -Wall -O2 -std=c++17
BUILD_DIR ?= build

SRCS = src/Server.cpp src/Logger.cpp
OBJS = $(BUILD_DIR)/src/Server.o $(BUILD_DIR)/src/Logger.o
LIB = $(BUILD_DIR)/libserver.a

.PHONY: all clean

all: $(LIB)


$(BUILD_DIR)/src/Server.o: src/Server.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/src/Logger.o: src/Logger.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	@ar rcs $@ $^

clean:
	@rm -f $(OBJS) $(LIB)
