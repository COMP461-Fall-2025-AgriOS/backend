#ifndef H_MAP
#define H_MAP

#include <vector>
#include <string>

class Map
{
private:
    int width;
    int height;
    std::vector<std::vector<int>> grid; // 0 = accessible, 1 = inaccessible

public:
    // Constructor that takes width and height
    Map(int width, int height);
    
    // Getter methods
    int getWidth() const;
    int getHeight() const;
    
    // Grid access methods
    int getCell(int x, int y) const;
    void setCell(int x, int y, int value);
    
    // Utility methods
    bool isValidPosition(int x, int y) const;
    bool isAccessible(int x, int y) const;
    
    // Initialize grid with all accessible cells (0s)
    void initializeEmpty();
    std::string serialize() const;
    // static Map deserialize(const std::string& data);
};

#endif

