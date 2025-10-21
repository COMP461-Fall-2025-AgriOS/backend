#include "Map.h"
#include <stdexcept>
#include <sstream>

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

void Map::addRobot(const Robot& robot)
{
    robots.push_back(robot);
}

void Map::removeRobot(const Robot& robot)
{
    robots.erase(robots.end());
}

// Serialize to JSON-like format
std::string Map::serialize() const
{
    std::ostringstream out;
    out << '{';
    out << "\"width\":" << width << ",";
    out << "\"height\":" << height << ",";
    out << "\"grid\":[";
    for (int y = 0; y < height; ++y)
    {
        out << "[";
        for (int x = 0; x < width; ++x)
        {
            out << grid[y][x];
            if (x < width - 1) out << ",";
        }
        out << "]";
        if (y < height - 1) out << ",";
    }
    out << "]";
    out << '}';
    return out.str();
}

std::string Map::serializeRobots() const
{
    std::ostringstream out;
    out << "[";
    for (const auto& robot : robots)
    {
        out << robot.serialize() << ",";
    }
    out << "]";
    return out.str();
}
