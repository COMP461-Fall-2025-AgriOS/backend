#ifndef H_TASK_MANAGER
#define H_TASK_MANAGER

#include "Task.h"
#include "Map.h"
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include <map>
#include <string>

// Forward declaration
using GridPoint = std::pair<int, int>;

class TaskManager
{
public:
    explicit TaskManager(Map& map);

    // === Task Input Methods (Flexible Interface) ===
    
    // Simple: just coordinates - auto-generates ID and default priority
    void addTask(const std::vector<float>& position);
    
    // With priority
    void addTask(const std::vector<float>& position, int priority);
    
    // With priority and description
    void addTask(const std::vector<float>& position, int priority, const std::string& description);
    
    // Batch add from coordinates
    void addTasks(const std::vector<std::vector<float>>& positions);
    
    // Batch add with priorities (vector of pairs: {position, priority})
    void addTasks(const std::vector<std::pair<std::vector<float>, int>>& tasksWithPriorities);
    
    // Full control (existing method)
    void addTask(const Task& task);

    // === Task Assignment Methods ===
    
    // Greedy assignment (existing methods)
    std::optional<std::string> assignNextTaskNearestRobot();
    std::optional<std::string> assignTaskNearestRobot(const Task& task);
    
    // Optimal assignment using Hungarian algorithm (pathfinding-aware)
    // Returns map of taskId -> robotId assignments
    std::map<std::string, std::string> assignAllTasksOptimal();
    
    // Balanced assignment considering makespan (good for concurrent execution)
    std::map<std::string, std::string> assignAllTasksBalanced();
    
    // === Query Methods ===
    
    std::vector<Task> getPendingTasks() const;
    std::optional<Task> getTaskById(const std::string& taskId) const;
    void markTaskComplete(const std::string& taskId);
    
    // Get current assignments
    const std::unordered_map<std::string, std::string>& getAssignments() const;

private:
    Map& mapRef;
    std::vector<Task> pendingTasks;
    std::unordered_map<std::string, std::string> taskAssignments; // taskId -> robotId
    int nextTaskIdCounter = 0; // For auto-generating task IDs

    // === Distance Calculation ===
    
    static float calculateDistance(const std::vector<float>& a, const std::vector<float>& b);
    static bool hasValidPosition(const std::vector<float>& position);
    
    // Pathfinding-aware distance calculation
    int computePathDistance(GridPoint start, GridPoint goal) const;
    std::vector<GridPoint> computePath(GridPoint start, GridPoint goal) const;
    
    // Helper to convert float position to grid point
    static GridPoint toGridPoint(const std::vector<float>& position);

    // === Robot Finding ===
    
    std::optional<std::reference_wrapper<Robot>> findNearestAvailableRobot(const std::vector<float>& target);
    std::vector<std::reference_wrapper<Robot>> getAvailableRobots() const;
    bool isRobotAvailable(const Robot& robot) const;

    // === Assignment Algorithms ===
    
    // Hungarian algorithm implementation
    std::map<std::string, std::string> hungarianAssignment(
        const std::vector<Task>& tasks,
        const std::vector<std::reference_wrapper<Robot>>& robots,
        std::function<float(const Robot&, const Task&)> costFunction
    ) const;
    
    // Cost function for pathfinding distance
    float pathfindingCost(const Robot& robot, const Task& task) const;
    
    // Cost function considering robot speed (for makespan)
    float makespanCost(const Robot& robot, const Task& task) const;
    
    // Helper: generate unique task ID
    std::string generateTaskId();
};

#endif

