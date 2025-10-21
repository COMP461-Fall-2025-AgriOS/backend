#ifndef H_ROBOT
#define H_ROBOT

#include <string>
#include <vector>

// Forward declaration for Map class
class Map;

struct Robot
{
    std::string name;
    std::string id;
    // These probably need their own data types later idk
    std::string type;
    std::string attributes;

    std::vector<float> position; // x, y
    float speed; // movement speed per step
    float maxDistance; // maximum distance the robot can travel

    // Movement methods
    std::vector<float> getPos() const;
    void setPosition(float x, float y);
    void setPosition(const std::vector<float>& newPos);
	void pathfind(const Map& map, const std::vector<float>& target);
    
    // Movement validation and execution
    bool canMoveTo(float x, float y, const Map& map) const;
    bool moveTo(float x, float y, const Map& map);
    bool moveBy(float dx, float dy, const Map& map);
    
    // Direction-based movement
    enum class Direction { UP, DOWN, LEFT, RIGHT, UP_LEFT, UP_RIGHT, DOWN_LEFT, DOWN_RIGHT };
    bool moveInDirection(Direction dir, const Map& map);
    
    // Grid-based movement (snap to grid)
    bool moveToGrid(int gridX, int gridY, const Map& map);
    std::pair<int, int> getGridPosition() const;

    // Serialization JSON stuff ??
    std::string serialize() const;
    static Robot deserialize(const std::string& data);
    static std::vector<Robot> deserializeList(const std::string& data);
};

#endif