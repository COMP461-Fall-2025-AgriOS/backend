# Top-level Makefile for the reorganized project

CXX = g++
INCLUDES = -Iserver/include -Imodules/include -Iinternal-representations/include -Iplugins -I.
CXX = g++
CXXFLAGS = -I/usr/include $(INCLUDES) -Wall -O2 -pthread -std=c++17
LDFLAGS = -ldl

BUILD_DIR = build

SUBDIRS = server modules internal-representations
PLUGIN_EXAMPLES = $(wildcard plugins/examples/*)

LIB_SERVER = $(BUILD_DIR)/libserver.a
LIB_MODULES = $(BUILD_DIR)/libmodules.a
LIB_REPR = $(BUILD_DIR)/librepr.a

TARGET = agrios_backend

.PHONY: all build clean subdirs plugins

all: build

build: subdirs plugins $(TARGET)

subdirs: $(LIB_SERVER) $(LIB_MODULES) $(LIB_REPR)


$(LIB_SERVER):
	@$(MAKE) -C server BUILD_DIR=$(abspath $(BUILD_DIR))

$(LIB_MODULES):
	@$(MAKE) -C modules BUILD_DIR=$(abspath $(BUILD_DIR))

$(LIB_REPR):
	@$(MAKE) -C internal-representations BUILD_DIR=$(abspath $(BUILD_DIR))

plugins:
	@for d in $(PLUGIN_EXAMPLES); do \
		if [ -d "$$d" ]; then $(MAKE) -C $$d; fi; \
	done

$(TARGET): $(BUILD_DIR)/main.o $(LIB_SERVER) $(LIB_MODULES) $(LIB_REPR)
	@$(CXX) $(CXXFLAGS) $(BUILD_DIR)/main.o -L$(BUILD_DIR) -lserver -lmodules -lrepr -o $(TARGET) $(LDFLAGS)

$(BUILD_DIR)/main.o: main.cpp | $(BUILD_DIR)_dir
	@$(CXX) $(CXXFLAGS) -c main.cpp -o $(BUILD_DIR)/main.o

$(BUILD_DIR)_dir:
	@mkdir -p $(BUILD_DIR)

clean:
	@-rm -rf $(BUILD_DIR) $(TARGET)
	@for d in $(PLUGIN_EXAMPLES); do \
		if [ -d "$$d" ]; then $(MAKE) -C $$d clean; fi; \
	done