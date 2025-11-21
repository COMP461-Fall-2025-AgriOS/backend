#ifndef H_MAP
#define H_MAP

#include <vector>
#include <string>
#include "Robot.h"

class Map
{
private:
    int width;
    int height;
    std::vector<std::vector<int>> grid; // 0 = accessible, 1 = inaccessible
    std::vector<Robot> robots;

public:
    // Constructor that takes width and height
    Map(int width, int height);
    
    // Getter methods
    int getWidth() const;
    int getHeight() const;
    const std::vector<Robot>& getRobots() const;
    std::vector<Robot>& getRobots();
    Robot* findRobotById(const std::string& robotId);
    const Robot* findRobotById(const std::string& robotId) const;
    void addRobot(const Robot& robot);
    void removeRobot(const std::string& robotId);
    
    // Grid access methods
    int getCell(int x, int y) const;
    void setCell(int x, int y, int value);
    
    // Utility methods
    bool isValidPosition(int x, int y) const;
    bool isAccessible(int x, int y) const;
    
    // Initialize grid with all accessible cells (0s)
    void initializeEmpty();
    std::string serialize() const;
    std::string serializeRobots() const;
    // static Map deserialize(const std::string& data);
};

#endif

