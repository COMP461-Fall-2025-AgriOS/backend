#ifndef H_SIMULATION_LOGGER
#define H_SIMULATION_LOGGER

#include <string>
#include <mutex>
#include <fstream>
#include <chrono>
#include <vector>
#include <utility>

class SimulationLogger
{
public:
    explicit SimulationLogger(const std::string& filename = "simulation.log");
    ~SimulationLogger();

    void log(const std::string& msg);

    void logPlannerStart(const std::string& robotId, const std::string& robotName, int startX, int startY, int goalX, int goalY, int mapW, int mapH);
    void logExpandNode(int x, int y, int cost, int parentX, int parentY);
    void logPushNode(int x, int y, int cost);
    void logPathReconstructed(const std::vector<std::pair<int,int>>& path);
    void logMoveExecuted(int x, int y);

private:
    std::string filename_;
    std::ofstream ofs_;
    std::mutex mtx_;

    std::string timestamp();
    void writeLine(const std::string& line);
};

#endif
