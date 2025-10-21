# Compiler and flags
CXX = g++
CXXFLAGS = -I/usr/include -Wall -O2 -pthread -std=c++17 

# Main
SRC = main.cpp Robot.cpp Server.cpp Map.cpp Logger.cpp Module.cpp ModuleManager.cpp

OBJ = $(SRC:.cpp=.o)

# Executables
TARGET = agrios_backend

# Default target
all: $(TARGET)

build: all

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(TARGET) -ldl

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build directory and executables
clean:
	rm -rf $(OBJ) $(TARGET) core.*
