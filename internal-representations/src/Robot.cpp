#include "Robot.h"
#include "Map.h"
#include "SimulationLogger.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <cmath>
#include <queue>
#include <limits>

namespace {
    std::string escapeString(const std::string& input)
    {
        std::string result;
        result.reserve(input.size());
        for (char ch : input)
        {
            switch (ch)
            {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += ch; break;
            }
        }
        return result;
    }

    std::string unescapeString(const std::string& input)
    {
        std::string result;
        result.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i)
        {
            char ch = input[i];
            if (ch == '\\' && i + 1 < input.size())
            {
                char next = input[++i];
                switch (next)
                {
                    case '\\': result += '\\'; break;
                    case '"': result += '"'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += next; break;
                }
            }
            else
            {
                result += ch;
            }
        }
        return result;
    }

    void skipWhitespace(const std::string& s, size_t& i)
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        {
            ++i;
        }
    }

    bool consumeLiteral(const std::string& s, size_t& i, const std::string& literal)
    {
        if (s.compare(i, literal.size(), literal) == 0)
        {
            i += literal.size();
            return true;
        }
        return false;
    }

    // Very naive key finder: expects keys like "name":, "id":, etc.
    size_t findKey(const std::string& s, const std::string& key, size_t startPos)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = s.find(needle, startPos);
        return pos;
    }

    std::string parseStringValueByKey(const std::string& s, const std::string& key)
    {
        size_t pos = findKey(s, key, 0);
        if (pos == std::string::npos) return std::string();
        pos += key.size() + 2; // move past "key"
        // find ':'
        pos = s.find(':', pos);
        if (pos == std::string::npos) return std::string();
        ++pos;
        skipWhitespace(s, pos);
        if (pos >= s.size() || s[pos] != '"') return std::string();
        ++pos; // after opening quote
        std::string value;
        while (pos < s.size())
        {
            char ch = s[pos++];
            if (ch == '"')
            {
                break;
            }
            if (ch == '\\' && pos < s.size())
            {
                char esc = s[pos++];
                value.push_back('\\');
                value.push_back(esc);
                continue;
            }
            value.push_back(ch);
        }
        return unescapeString(value);
    }

    int parseIntValueByKey(const std::string& s, const std::string& key)
    {
        size_t pos = findKey(s, key, 0);
        if (pos == std::string::npos) return 0;
        pos += key.size() + 2;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return 0;
        ++pos;
        skipWhitespace(s, pos);
        // read until non-number char
        bool negative = false;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+'))
        {
            negative = (s[pos] == '-');
            ++pos;
        }
        long long value = 0;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        {
            value = value * 10 + (s[pos] - '0');
            ++pos;
        }
        return static_cast<int>(negative ? -value : value);
    }

    std::vector<float> parseFloatArrayByKey(const std::string& s, const std::string& key)
    {
        std::vector<float> out;
        size_t pos = findKey(s, key, 0);
        if (pos == std::string::npos) return out;
        pos += key.size() + 2;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return out;
        ++pos;
        skipWhitespace(s, pos);
        if (pos >= s.size() || s[pos] != '[') return out;
        ++pos; // after '['
        while (pos < s.size())
        {
            skipWhitespace(s, pos);
            // end of array
            if (pos < s.size() && s[pos] == ']')
            {
                ++pos;
                break;
            }
            // parse number
            size_t start = pos;
            // allow signs, decimal, exponent
            bool seenDigit = false;
            while (pos < s.size())
            {
                char ch = s[pos];
                if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E')
                {
                    if (std::isdigit(static_cast<unsigned char>(ch))) seenDigit = true;
                    ++pos;
                }
                else
                {
                    break;
                }
            }
            if (pos > start && seenDigit)
            {
                try
                {
                    float val = std::stof(s.substr(start, pos - start));
                    out.push_back(val);
                }
                catch (...)
                {
                    // ignore malformed values
                }
            }
            skipWhitespace(s, pos);
            if (pos < s.size() && s[pos] == ',')
            {
                ++pos;
                continue;
            }
            else if (pos < s.size() && s[pos] == ']')
            {
                ++pos;
                break;
            }
            else
            {
                // malformed; try to break to avoid infinite loop
                break;
            }
        }
        return out;
    }
}

std::string Robot::serialize() const
{
    std::ostringstream out;
    out << '{';
    out << "\"name\":\"" << escapeString(name) << "\",";
    out << "\"id\":\"" << escapeString(id) << "\",";
    out << "\"type\":\"" << escapeString(type) << "\",";
    out << "\"attributes\":\"" << escapeString(attributes) << "\",";
    out << "\"position\":[";
    for (size_t i = 0; i < position.size(); ++i)
    {
        if (i > 0) out << ',';
        // Use default float formatting; could be adjusted if needed
        out << std::setprecision(6) << std::noshowpoint << position[i];
    }
    out << "]";
    out << '}';
    return out.str();
}

std::vector<float> Robot::getPos() const
{
    return position;
}

void Robot::setPosition(float x, float y)
{
    position[0] = x;
    position[1] = y;
}

void Robot::setPosition(const std::vector<float>& newPos)
{
    if (newPos.size() >= 2)
    {
        position[0] = newPos[0];
        position[1] = newPos[1];
    }
}

bool Robot::canMoveTo(float x, float y, const Map& map) const
{
    // Check if the position is within map bounds
    if (x < 0 || x >= map.getWidth() || y < 0 || y >= map.getHeight())
    {
        return false;
    }
    
    // Check if the position is accessible
    if (!map.isAccessible(static_cast<int>(x), static_cast<int>(y)))
    {
        return false;
    }
    
    // Check distance constraint if maxDistance is set
    if (maxDistance > 0)
    {
        float currentX = position[0];
        float currentY = position[1];
        float distance = std::sqrt((x - currentX) * (x - currentX) + (y - currentY) * (y - currentY));
        if (distance > maxDistance)
        {
            return false;
        }
    }
    
    return true;
}

bool Robot::moveTo(float x, float y, const Map& map)
{
    if (canMoveTo(x, y, map))
    {
        setPosition(x, y);
        return true;
    }
    return false;
}

bool Robot::moveBy(float dx, float dy, const Map& map)
{
    float newX = position[0] + dx;
    float newY = position[1] + dy;
    return moveTo(newX, newY, map);
}

bool Robot::moveInDirection(Direction dir, const Map& map)
{
    float dx = 0, dy = 0;
    float moveDistance = 1;
    
    switch (dir)
    {
        case Direction::UP: dy = -moveDistance; break;
        case Direction::DOWN: dy = moveDistance; break;
        case Direction::LEFT: dx = -moveDistance; break;
        case Direction::RIGHT: dx = moveDistance; break;
        case Direction::UP_LEFT: dx = -moveDistance; dy = -moveDistance; break;
        case Direction::UP_RIGHT: dx = moveDistance; dy = -moveDistance; break;
        case Direction::DOWN_LEFT: dx = -moveDistance; dy = moveDistance; break;
        case Direction::DOWN_RIGHT: dx = moveDistance; dy = moveDistance; break;
    }
    
    return moveBy(dx, dy, map);
}

bool Robot::moveToGrid(int gridX, int gridY, const Map& map)
{
    return moveTo(static_cast<float>(gridX), static_cast<float>(gridY), map);
}

std::pair<int, int> Robot::getGridPosition() const
{
    return std::make_pair(static_cast<int>(std::round(position[0])), 
                         static_cast<int>(std::round(position[1])));
}

Robot Robot::deserialize(const std::string& data)
{
    Robot r;
    r.name = parseStringValueByKey(data, "name");
    r.id = parseStringValueByKey(data, "id");
    r.type = parseStringValueByKey(data, "type");
    r.attributes = parseStringValueByKey(data, "attributes");
    r.position = parseFloatArrayByKey(data, "position");
    return r;
}

std::vector<Robot> Robot::deserializeList(const std::string& data) {
    std::vector<Robot> robots;
    size_t pos = 0;
    while ((pos = data.find('{', pos)) != std::string::npos) {
        size_t endPos = data.find('}', pos);
        if (endPos != std::string::npos) {
            std::string robotData = data.substr(pos, endPos - pos + 1);
            robots.push_back(deserialize(robotData));
            pos = endPos + 1;
        } else {
            break;
        }
    }
    return robots;
}

void Robot::pathfind(const Map& map, const std::vector<float>& target)
{
    if (target.size() < 2) return;

    // Convert to grid coordinates
    std::pair<int, int> start = getGridPosition();
    int goalX = static_cast<int>(std::round(target[0]));
    int goalY = static_cast<int>(std::round(target[1]));

    // Bounds and accessibility checks
    if (goalX < 0 || goalX >= map.getWidth() || goalY < 0 || goalY >= map.getHeight()) return;
    if (!map.isAccessible(goalX, goalY)) return;
    if (start.first == goalX && start.second == goalY) return;

    const int width = map.getWidth();
    const int height = map.getHeight();
    const int total = width * height;

    // Create a simulation logger instance (append to simulation.log)
    SimulationLogger simlog("simulation.log");
    simlog.logPlannerStart(id, name, this->getGridPosition().first, this->getGridPosition().second, goalX, goalY, width, height);

    auto indexOf = [width](int x, int y) { return y * width + x; };

    // Dijkstra structures
    std::vector<int> dist(total, std::numeric_limits<int>::max());
    std::vector<int> prev(total, -1);

    struct Node { int cost; int x; int y; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.cost > b.cost; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    int sx = start.first;
    int sy = start.second;
    dist[indexOf(sx, sy)] = 0;
    pq.push({0, sx, sy});

    // 4 directional movement, 1 cost per step
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (!pq.empty())
    {
        Node cur = pq.top();
        pq.pop();

        // Log expansion
        int parentX = -1, parentY = -1;
        int curIdx = indexOf(cur.x, cur.y);
        if (prev[curIdx] != -1) { parentX = prev[curIdx] % width; parentY = prev[curIdx] / width; }
        simlog.logExpandNode(cur.x, cur.y, cur.cost, parentX, parentY);

        if (cur.x == goalX && cur.y == goalY) break;

        int curIdx2 = indexOf(cur.x, cur.y);
        if (cur.cost != dist[curIdx2]) continue; // stale

        for (int dir = 0; dir < 4; ++dir)
        {
            int nx = cur.x + dx[dir];
            int ny = cur.y + dy[dir];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (!map.isAccessible(nx, ny)) continue;

            int nIdx = indexOf(nx, ny);
            int nCost = cur.cost + 1;
            if (nCost < dist[nIdx])
            {
                dist[nIdx] = nCost;
                prev[nIdx] = curIdx;
                pq.push({nCost, nx, ny});
                simlog.logPushNode(nx, ny, nCost);
            }
        }
    }

    // Reconstruct path
    int goalIdx = indexOf(goalX, goalY);
    if (prev[goalIdx] == -1) return; // unreachable

    std::vector<std::pair<int,int>> path;
    for (int at = goalIdx; at != -1; at = prev[at])
    {
        int x = at % width;
        int y = at / width;
        path.emplace_back(x, y);
    }
    std::reverse(path.begin(), path.end());

    simlog.logPathReconstructed(path);

    // Move along the path
    for (size_t i = 1; i < path.size(); ++i)
    {
        const auto& step = path[i];
        if (!moveToGrid(step.first, step.second, map))
        {
            // If a step becomes invalid stop
            break;
        }
        simlog.logMoveExecuted(step.first, step.second);
    }
}