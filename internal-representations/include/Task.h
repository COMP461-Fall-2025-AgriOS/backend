#ifndef H_TASK
#define H_TASK

#include <string>
#include <vector>
#include <map>

enum class TaskStatus
{
    Pending,
    Assigned,
    InProgress,
    Completed,
    Failed
};

struct Task
{
    std::string id;
    std::string description;
    std::vector<float> targetPosition; // Expected to contain at least x and y
    TaskStatus status = TaskStatus::Pending;
    int priority = 0;
    std::vector<std::string> moduleIds; // Plugins/modules to invoke when task completes (at this location)
};

#endif

