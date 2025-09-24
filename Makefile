# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -O2

# Main
SRC = main.cpp Robot.cpp Server.cpp

OBJ = $(SRC:.cpp=.o)

# Executables
TARGET = agrios_backend

# Default target
all: $(TARGET)

build: all

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build directory and executables
clean:
	rm -rf $(OBJ) $(TARGET)
