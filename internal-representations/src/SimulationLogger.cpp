#include "../include/SimulationLogger.h"
#include <ctime>
#include <sstream>
#include <iomanip>

SimulationLogger::SimulationLogger(const std::string& filename)
    : filename_(filename)
{
    ofs_.open(filename_, std::ofstream::out | std::ofstream::app);
}

SimulationLogger::~SimulationLogger()
{
    if (ofs_.is_open()) ofs_.close();
}

std::string SimulationLogger::timestamp()
{
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm;
    localtime_r(&t, &tm);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void SimulationLogger::writeLine(const std::string& line)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (ofs_.is_open())
    {
        ofs_ << line << '\n';
        ofs_.flush();
    }
}

void SimulationLogger::log(const std::string& msg)
{
    writeLine(timestamp() + " " + msg);
}

void SimulationLogger::logPlannerStart(const std::string& robotId, const std::string& robotName, int startX, int startY, int goalX, int goalY, int mapW, int mapH)
{
    std::ostringstream ss;
    ss << "robotId=\"" << robotId << "\" PLANNER_START robotName=\"" << robotName << "\" start=(" << startX << "," << startY << ") goal=(" << goalX << "," << goalY << ") map=(" << mapW << "x" << mapH << ")";
    writeLine(timestamp() + " " + ss.str());
}

void SimulationLogger::logExpandNode(const std::string& robotId, int x, int y, int cost, int parentX, int parentY)
{
    std::ostringstream ss;
    ss << "robotId=\"" << robotId << "\" EXPAND x=" << x << " y=" << y << " cost=" << cost << " parent=(" << parentX << "," << parentY << ")";
    writeLine(timestamp() + " " + ss.str());
}

void SimulationLogger::logPushNode(const std::string& robotId, int x, int y, int cost)
{
    std::ostringstream ss;
    ss << "robotId=\"" << robotId << "\" PUSH x=" << x << " y=" << y << " cost=" << cost;
    writeLine(timestamp() + " " + ss.str());
}

void SimulationLogger::logPathReconstructed(const std::string& robotId, const std::vector<std::pair<int,int>>& path)
{
    std::ostringstream ss;
    ss << "robotId=\"" << robotId << "\" PATH size=" << path.size() << " coords=";
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i) ss << ";";
        ss << "(" << path[i].first << "," << path[i].second << ")";
    }
    writeLine(timestamp() + " " + ss.str());
}

void SimulationLogger::logMoveExecuted(const std::string& robotId, int x, int y)
{
    std::ostringstream ss;
    ss << "robotId=\"" << robotId << "\" MOVE_EXECUTED x=" << x << " y=" << y;
    writeLine(timestamp() + " " + ss.str());
}
