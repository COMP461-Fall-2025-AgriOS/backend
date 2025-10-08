#include "Map.h"
#include <stdexcept>

Map::Map(int width, int height) : width(width), height(height)
{
    // Validate dimensions
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Map dimensions must be positive");
    }
    
    // Initialize the 2D grid with the specified dimensions
    grid.resize(height);
    for (int i = 0; i < height; ++i)
    {
        grid[i].resize(width, 0); // Initialize all cells as accessible for now
    }
}

int Map::getWidth() const
{
    return width;
}

int Map::getHeight() const
{
    return height;
}

int Map::getCell(int x, int y) const
{
    if (!isValidPosition(x, y))
    {
        throw std::out_of_range("Position is out of bounds");
    }
    return grid[y][x];
}

void Map::setCell(int x, int y, int value)
{
    if (!isValidPosition(x, y))
    {
        throw std::out_of_range("Position is out of bounds");
    }
    grid[y][x] = value;
}

bool Map::isValidPosition(int x, int y) const
{
    return x >= 0 && x < width && y >= 0 && y < height;
}

bool Map::isAccessible(int x, int y) const
{
    if (!isValidPosition(x, y))
    {
        return false;
    }
    return grid[y][x] == 0; // 0 means accessible
}

void Map::initializeEmpty()
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            grid[y][x] = 0; // Set all cells as accessible
        }
    }
}
