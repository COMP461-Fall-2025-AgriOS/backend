CC = g++
CFLAGS = -Iinclude -I../modules/include -I.. -I../.. -fPIC -Wall -O2 -std=c++17
BUILD_DIR ?= build

SRCS = src/Map.cpp src/Robot.cpp src/SimulationLogger.cpp src/TaskManager.cpp
OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/src/%.o,$(SRCS))
LIB = $(BUILD_DIR)/librepr.a

.PHONY: all clean

all: $(LIB)

$(BUILD_DIR)/src/%.o: src/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	@ar rcs $@ $^

clean:
	@rm -f $(OBJS) $(LIB)
