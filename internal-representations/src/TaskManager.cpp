#include "TaskManager.h"
#include "SimulationLogger.h"
#include <limits>
#include <cmath>
#include <algorithm>
#include <queue>
#include <set>
#include <sstream>
#include <iomanip>

TaskManager::TaskManager(Map& map)
    : mapRef(map)
{
}

void TaskManager::addTask(const Task& task)
{
    pendingTasks.push_back(task);
}

std::optional<std::string> TaskManager::assignNextTaskNearestRobot()
{
    if (pendingTasks.empty())
    {
        return std::nullopt;
    }

    std::sort(
        pendingTasks.begin(),
        pendingTasks.end(),
        [](const Task& lhs, const Task& rhs)
        {
            if (lhs.priority == rhs.priority)
            {
                return lhs.id < rhs.id;
            }
            return lhs.priority > rhs.priority;
        });

    Task& task = pendingTasks.front();
    std::string taskId = task.id;

    std::optional<std::string> result = assignTaskNearestRobot(task);

    // Remove the task from pending list only if assignment succeeded
    if (result.has_value())
    {
        pendingTasks.erase(pendingTasks.begin());
    }

    return result;
}

std::optional<std::string> TaskManager::assignTaskNearestRobot(const Task& task)
{
    if (!hasValidPosition(task.targetPosition))
    {
        return std::nullopt;
    }

    auto nearestRobotRef = findNearestAvailableRobot(task.targetPosition);
    if (!nearestRobotRef.has_value())
    {
        return std::nullopt;
    }

    Robot& robot = nearestRobotRef.value().get();

    // Find the task in pending list and mark as assigned
    auto taskIt = std::find_if(pendingTasks.begin(), pendingTasks.end(),
        [&task](const Task& t)
        {
            return t.id == task.id;
        });

    if (taskIt != pendingTasks.end())
    {
        taskIt->status = TaskStatus::Assigned;
    }

    taskAssignments[task.id] = robot.id;
    robot.pathfind(mapRef, task.targetPosition, task.moduleIds);

    return robot.id;
}

std::vector<Task> TaskManager::getPendingTasks() const
{
    return pendingTasks;
}

std::optional<Task> TaskManager::getTaskById(const std::string& taskId) const
{
    auto it = std::find_if(
        pendingTasks.begin(),
        pendingTasks.end(),
        [&taskId](const Task& task)
        {
            return task.id == taskId;
        });

    if (it != pendingTasks.end())
    {
        return *it;
    }

    return std::nullopt;
}

void TaskManager::markTaskComplete(const std::string& taskId)
{
    taskAssignments.erase(taskId);
}

float TaskManager::calculateDistance(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() < 2 || b.size() < 2)
    {
        return std::numeric_limits<float>::max();
    }

    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    return std::sqrt(dx * dx + dy * dy);
}

bool TaskManager::hasValidPosition(const std::vector<float>& position)
{
    return position.size() >= 2 &&
           !std::isnan(position[0]) && !std::isnan(position[1]) &&
           std::isfinite(position[0]) && std::isfinite(position[1]);
}

std::optional<std::reference_wrapper<Robot>> TaskManager::findNearestAvailableRobot(const std::vector<float>& target)
{
    auto& robots = mapRef.getRobots();
    if (robots.empty())
    {
        return std::nullopt;
    }

    Robot* bestRobot = nullptr;
    float bestDistance = std::numeric_limits<float>::max();

    for (auto& robot : robots)
    {
        if (!hasValidPosition(robot.position))
        {
            continue;
        }

        // Skip robots already assigned to a task
        auto existingAssignment = std::find_if(
            taskAssignments.begin(),
            taskAssignments.end(),
            [&robot](const auto& entry)
            {
                return entry.second == robot.id;
            });

        if (existingAssignment != taskAssignments.end())
        {
            continue;
        }

        float distance = calculateDistance(robot.position, target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestRobot = &robot;
        }
    }

    if (bestRobot == nullptr)
    {
        return std::nullopt;
    }

    return *bestRobot;
}

// === Task Input Methods ===

std::string TaskManager::generateTaskId()
{
    std::ostringstream oss;
    oss << "task-" << nextTaskIdCounter++;
    return oss.str();
}

void TaskManager::addTask(const std::vector<float>& position)
{
    addTask(position, 0, "");
}

void TaskManager::addTask(const std::vector<float>& position, int priority)
{
    addTask(position, priority, "");
}

void TaskManager::addTask(const std::vector<float>& position, int priority, const std::string& description)
{
    if (!hasValidPosition(position))
    {
        return; // Silently skip invalid positions
    }
    
    Task task;
    task.id = generateTaskId();
    task.description = description.empty() ? "Task at (" + std::to_string(position[0]) + ", " + std::to_string(position[1]) + ")" : description;
    task.targetPosition = position;
    task.status = TaskStatus::Pending;
    task.priority = priority;
    
    pendingTasks.push_back(task);
}

void TaskManager::addTasks(const std::vector<std::vector<float>>& positions)
{
    for (const auto& position : positions)
    {
        addTask(position);
    }
}

void TaskManager::addTasks(const std::vector<std::pair<std::vector<float>, int>>& tasksWithPriorities)
{
    for (const auto& [position, priority] : tasksWithPriorities)
    {
        addTask(position, priority);
    }
}


GridPoint TaskManager::toGridPoint(const std::vector<float>& position)
{
    if (position.size() < 2)
    {
        return {0, 0};
    }
    return {static_cast<int>(std::lround(position[0])), static_cast<int>(std::lround(position[1]))};
}

std::vector<GridPoint> TaskManager::computePath(GridPoint start, GridPoint goal) const
{
    if (start == goal)
    {
        return {start};
    }

    const int width = mapRef.getWidth();
    const int height = mapRef.getHeight();

    if (goal.first < 0 || goal.first >= width || goal.second < 0 || goal.second >= height)
    {
        return {};
    }
    if (!mapRef.isAccessible(goal.first, goal.second) || !mapRef.isAccessible(start.first, start.second))
    {
        return {};
    }

    const int total = width * height;
    auto indexOf = [width](int x, int y)
    {
        return y * width + x;
    };

    std::vector<int> dist(total, std::numeric_limits<int>::max());
    std::vector<int> prev(total, -1);

    struct Node
    {
        int cost;
        int x;
        int y;
    };
    struct NodeCompare
    {
        bool operator()(const Node& lhs, const Node& rhs) const
        {
            return lhs.cost > rhs.cost;
        }
    };

    std::priority_queue<Node, std::vector<Node>, NodeCompare> pq;
    dist[indexOf(start.first, start.second)] = 0;
    pq.push({0, start.first, start.second});

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (!pq.empty())
    {
        Node cur = pq.top();
        pq.pop();

        if (cur.x == goal.first && cur.y == goal.second)
        {
            break;
        }

        int idx = indexOf(cur.x, cur.y);
        if (cur.cost != dist[idx])
        {
            continue;
        }

        for (int dir = 0; dir < 4; ++dir)
        {
            int nx = cur.x + dx[dir];
            int ny = cur.y + dy[dir];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height)
            {
                continue;
            }
            if (!mapRef.isAccessible(nx, ny))
            {
                continue;
            }

            int nIdx = indexOf(nx, ny);
            int nCost = cur.cost + 1;
            if (nCost < dist[nIdx])
            {
                dist[nIdx] = nCost;
                prev[nIdx] = idx;
                pq.push({nCost, nx, ny});
            }
        }
    }

    int goalIdx = indexOf(goal.first, goal.second);
    if (prev[goalIdx] == -1)
    {
        return {};
    }

    std::vector<GridPoint> path;
    for (int at = goalIdx; at != -1; at = prev[at])
    {
        int x = at % width;
        int y = at / width;
        path.emplace_back(x, y);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

int TaskManager::computePathDistance(GridPoint start, GridPoint goal) const
{
    auto path = computePath(start, goal);
    if (path.empty())
    {
        // Unreachable - use a large but reasonable penalty instead of INT_MAX
        // to avoid overflow when converting to float
        return 999999;
    }
    return static_cast<int>(path.size() - 1); // Distance is number of steps
}

// === Robot Availability Methods ===

bool TaskManager::isRobotAvailable(const Robot& robot) const
{
    if (!hasValidPosition(robot.position))
    {
        return false;
    }

    // Check if robot is already assigned to a task
    auto existingAssignment = std::find_if(
        taskAssignments.begin(),
        taskAssignments.end(),
        [&robot](const auto& entry)
        {
            return entry.second == robot.id;
        });

    return existingAssignment == taskAssignments.end();
}

std::vector<std::reference_wrapper<Robot>> TaskManager::getAvailableRobots() const
{
    std::vector<std::reference_wrapper<Robot>> available;
    auto& robots = mapRef.getRobots();
    
    for (auto& robot : robots)
    {
        if (isRobotAvailable(robot))
        {
            available.push_back(std::ref(robot));
        }
    }
    
    return available;
}

float TaskManager::pathfindingCost(const Robot& robot, const Task& task) const
{
    GridPoint robotPos = toGridPoint(robot.position);
    GridPoint taskPos = toGridPoint(task.targetPosition);
    
    int distance = computePathDistance(robotPos, taskPos);
    
    // Add priority penalty (higher priority = lower cost)
    float priorityPenalty = -task.priority * 10.0f;
    
    return static_cast<float>(distance) + priorityPenalty;
}

float TaskManager::makespanCost(const Robot& robot, const Task& task) const
{
    GridPoint robotPos = toGridPoint(robot.position);
    GridPoint taskPos = toGridPoint(task.targetPosition);

    int distance = computePathDistance(robotPos, taskPos);


    float timeCost = distance > 0 && robot.speed > 0.0f
        ? static_cast<float>(distance) / robot.speed
        : static_cast<float>(distance);

    // Add priority penalty
    float priorityPenalty = -task.priority * 10.0f;

    return timeCost + priorityPenalty;
}


// === Hungarian Algorithm Implementation ===

std::map<std::string, std::string> TaskManager::hungarianAssignment(
    const std::vector<Task>& tasks,
    const std::vector<std::reference_wrapper<Robot>>& robots,
    std::function<float(const Robot&, const Task&)> costFunction
) const
{
    std::map<std::string, std::string> assignments;

    if (tasks.empty() || robots.empty())
    {
        return assignments;
    }

    const size_t numTasks = tasks.size();
    const size_t numRobots = robots.size();

    // If we have more tasks than robots, we can only assign to available robots
    // If we have more robots than tasks, we'll create a square matrix
    const size_t n = std::max(numTasks, numRobots);

    // Build cost matrix
    std::vector<std::vector<float>> cost(n, std::vector<float>(n, std::numeric_limits<float>::max()));

    SimulationLogger("simulation.log").log("DEBUG: hungarianAssignment called with " + std::to_string(numTasks) + " tasks, " + std::to_string(numRobots) + " robots");

    for (size_t i = 0; i < numTasks; ++i)
    {
        for (size_t j = 0; j < numRobots; ++j)
        {
            cost[i][j] = costFunction(robots[j].get(), tasks[i]);
            SimulationLogger("simulation.log").log("DEBUG: cost[" + std::to_string(i) + "][" + std::to_string(j) + "] = " + std::to_string(cost[i][j]) + " (robot=" + robots[j].get().id + ", task=" + tasks[i].id + ")");
        }
    }
    
    std::vector<bool> taskAssigned(numTasks, false);
    std::vector<bool> robotAssigned(numRobots, false);
    
    // Sort all possible assignments by cost
    struct Assignment
    {
        size_t taskIdx;
        size_t robotIdx;
        float cost;
    };
    
    std::vector<Assignment> allAssignments;
    for (size_t i = 0; i < numTasks; ++i)
    {
        for (size_t j = 0; j < numRobots; ++j)
        {
            allAssignments.push_back({i, j, cost[i][j]});
        }
    }
    
    // Sort by cost
    std::sort(allAssignments.begin(), allAssignments.end(),
        [](const Assignment& a, const Assignment& b)
        {
            return a.cost < b.cost;
        });
    
    // Greedily assign tasks to robots
    int assignmentCount = 0;
    for (const auto& assignment : allAssignments)
    {
        if (!taskAssigned[assignment.taskIdx] && !robotAssigned[assignment.robotIdx])
        {
            assignments[tasks[assignment.taskIdx].id] = robots[assignment.robotIdx].get().id;
            taskAssigned[assignment.taskIdx] = true;
            robotAssigned[assignment.robotIdx] = true;
            assignmentCount++;
            SimulationLogger("simulation.log").log("DEBUG: Assigned task " + tasks[assignment.taskIdx].id + " to robot " + robots[assignment.robotIdx].get().id + " (cost=" + std::to_string(assignment.cost) + ")");
        }
    }

    SimulationLogger("simulation.log").log("DEBUG: Total assignments made: " + std::to_string(assignmentCount) + " out of " + std::to_string(numTasks) + " tasks");

    return assignments;
}


std::map<std::string, std::string> TaskManager::assignAllTasksOptimal()
{
    std::map<std::string, std::string> assignments;
    
    if (pendingTasks.empty())
    {
        return assignments;
    }
    
    // Sort tasks by priority (higher priority first)
    std::vector<Task> sortedTasks = pendingTasks;
    std::sort(sortedTasks.begin(), sortedTasks.end(),
        [](const Task& lhs, const Task& rhs)
        {
            if (lhs.priority == rhs.priority)
            {
                return lhs.id < rhs.id;
            }
            return lhs.priority > rhs.priority;
        });
    
    // Get available robots
    auto availableRobots = getAvailableRobots();
    
    if (availableRobots.empty())
    {
        return assignments;
    }
    
    // Use Hungarian algorithm with pathfinding cost
    assignments = hungarianAssignment(
        sortedTasks,
        availableRobots,
        [this](const Robot& robot, const Task& task)
        {
            return this->pathfindingCost(robot, task);
        }
    );
    
    // Apply assignments
    std::vector<std::string> assignedTaskIds;
    for (const auto& [taskId, robotId] : assignments)
    {
        // Find the task and update its status
        auto taskIt = std::find_if(pendingTasks.begin(), pendingTasks.end(),
            [&taskId](const Task& task)
            {
                return task.id == taskId;
            });

        if (taskIt != pendingTasks.end())
        {
            taskIt->status = TaskStatus::Assigned;
            taskAssignments[taskId] = robotId; // Update assignments map
            assignedTaskIds.push_back(taskId);

            // Find the robot and trigger pathfinding
            Robot* robot = mapRef.findRobotById(robotId);
            if (robot)
            {
                robot->pathfind(mapRef, taskIt->targetPosition, taskIt->moduleIds);
            }
        }
    }

    // Remove assigned tasks from pending list
    pendingTasks.erase(
        std::remove_if(pendingTasks.begin(), pendingTasks.end(),
            [&assignedTaskIds](const Task& task)
            {
                return std::find(assignedTaskIds.begin(), assignedTaskIds.end(), task.id) != assignedTaskIds.end();
            }),
        pendingTasks.end()
    );

    return assignments;
}

std::map<std::string, std::string> TaskManager::assignAllTasksBalanced()
{
    std::map<std::string, std::string> assignments;
    
    if (pendingTasks.empty())
    {
        return assignments;
    }
    
    // Sort tasks by priority
    std::vector<Task> sortedTasks = pendingTasks;
    std::sort(sortedTasks.begin(), sortedTasks.end(),
        [](const Task& lhs, const Task& rhs)
        {
            if (lhs.priority == rhs.priority)
            {
                return lhs.id < rhs.id;
            }
            return lhs.priority > rhs.priority;
        });
    
    // Get available robots
    auto availableRobots = getAvailableRobots();
    
    if (availableRobots.empty())
    {
        return assignments;
    }
    
    // Use Hungarian algorithm with makespan cost
    assignments = hungarianAssignment(
        sortedTasks,
        availableRobots,
        [this](const Robot& robot, const Task& task)
        {
            return this->makespanCost(robot, task);
        }
    );
    
    // Apply assignments
    std::vector<std::string> assignedTaskIds;
    for (const auto& [taskId, robotId] : assignments)
    {
        auto taskIt = std::find_if(pendingTasks.begin(), pendingTasks.end(),
            [&taskId](const Task& task)
            {
                return task.id == taskId;
            });

        if (taskIt != pendingTasks.end())
        {
            taskIt->status = TaskStatus::Assigned;
            taskAssignments[taskId] = robotId; // Update assignments map
            assignedTaskIds.push_back(taskId);

            Robot* robot = mapRef.findRobotById(robotId);
            if (robot)
            {
                robot->pathfind(mapRef, taskIt->targetPosition, taskIt->moduleIds);
            }
        }
    }

    // Remove assigned tasks from pending list
    pendingTasks.erase(
        std::remove_if(pendingTasks.begin(), pendingTasks.end(),
            [&assignedTaskIds](const Task& task)
            {
                return std::find(assignedTaskIds.begin(), assignedTaskIds.end(), task.id) != assignedTaskIds.end();
            }),
        pendingTasks.end()
    );

    return assignments;
}


// === Query Methods ===

const std::unordered_map<std::string, std::string>& TaskManager::getAssignments() const
{
    return taskAssignments;
}

void TaskManager::clearAllAssignments()
{
    taskAssignments.clear();
}

