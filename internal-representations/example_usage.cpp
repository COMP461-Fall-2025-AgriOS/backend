// Example usage of the new TaskManager interface
// This demonstrates the flexible task input and optimal assignment features

#include "TaskManager.h"
#include "Map.h"
#include <iostream>

int main()
{
    // Create a map
    Map map(20, 15);
    map.initializeEmpty();
    
    // Add some obstacles
    for (int x = 5; x < 15; ++x)
    {
        map.setCell(x, 7, 1);
    }
    
    // Create robots
    Robot robot1;
    robot1.id = "robot-1";
    robot1.name = "Robot 1";
    robot1.position = {2.0f, 2.0f};
    robot1.speed = 1.0f;
    map.addRobot(robot1);
    
    Robot robot2;
    robot2.id = "robot-2";
    robot2.name = "Robot 2";
    robot2.position = {18.0f, 12.0f};
    robot2.speed = 1.5f; // Faster robot
    map.addRobot(robot2);
    
    // Create TaskManager
    TaskManager taskManager(map);
    
    // === Example 1: Simple coordinate-based task input ===
    std::cout << "=== Example 1: Simple coordinate input ===\n";
    taskManager.addTask({3.0f, 5.0f});
    taskManager.addTask({10.0f, 8.0f});
    taskManager.addTask({15.0f, 3.0f});
    
    std::cout << "Added 3 tasks using simple coordinate input\n";
    std::cout << "Pending tasks: " << taskManager.getPendingTasks().size() << "\n\n";
    
    // === Example 2: Tasks with priorities ===
    std::cout << "=== Example 2: Tasks with priorities ===\n";
    taskManager.addTask({8.0f, 10.0f}, 5);  // High priority
    taskManager.addTask({12.0f, 2.0f}, 2);  // Medium priority
    taskManager.addTask({1.0f, 1.0f}, 1);   // Low priority
    
    std::cout << "Added 3 tasks with priorities\n";
    std::cout << "Pending tasks: " << taskManager.getPendingTasks().size() << "\n\n";
    
    // === Example 3: Batch task input ===
    std::cout << "=== Example 3: Batch task input ===\n";
    std::vector<std::vector<float>> batchTasks = {
        {4.0f, 4.0f},
        {6.0f, 6.0f},
        {8.0f, 8.0f}
    };
    taskManager.addTasks(batchTasks);
    
    std::cout << "Added 3 tasks in batch\n";
    std::cout << "Pending tasks: " << taskManager.getPendingTasks().size() << "\n\n";
    
    // === Example 4: Batch with priorities ===
    std::cout << "=== Example 4: Batch with priorities ===\n";
    std::vector<std::pair<std::vector<float>, int>> prioritizedTasks = {
        {{14.0f, 14.0f}, 10},  // Highest priority
        {{16.0f, 1.0f}, 3},
        {{1.0f, 14.0f}, 1}
    };
    taskManager.addTasks(prioritizedTasks);
    
    std::cout << "Added 3 tasks in batch with priorities\n";
    std::cout << "Pending tasks: " << taskManager.getPendingTasks().size() << "\n\n";
    
    // === Example 5: Optimal assignment ===
    std::cout << "=== Example 5: Optimal task assignment ===\n";
    auto assignments = taskManager.assignAllTasksOptimal();
    
    std::cout << "Assigned " << assignments.size() << " tasks to robots:\n";
    for (const auto& [taskId, robotId] : assignments)
    {
        std::cout << "  Task " << taskId << " -> Robot " << robotId << "\n";
    }
    std::cout << "\n";
    
    // === Example 6: Balanced assignment (makespan optimization) ===
    std::cout << "=== Example 6: Balanced assignment (for concurrent execution) ===\n";
    
    // Clear previous assignments and add new tasks
    TaskManager taskManager2(map);
    taskManager2.addTask({5.0f, 5.0f}, 3);
    taskManager2.addTask({10.0f, 10.0f}, 2);
    taskManager2.addTask({15.0f, 5.0f}, 1);
    
    auto balancedAssignments = taskManager2.assignAllTasksBalanced();
    
    std::cout << "Balanced assignment (considers robot speeds):\n";
    for (const auto& [taskId, robotId] : balancedAssignments)
    {
        std::cout << "  Task " << taskId << " -> Robot " << robotId << "\n";
    }
    
    return 0;
}

