#pragma once

#include "dashboard.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

class DashboardSimple : public Dashboard
{
    using State = StatusDownloader::State;
public:
    virtual void update(std::shared_ptr<const Task>, const StatusDownloader&) override;
};

void DashboardSimple::update(std::shared_ptr<const Task> task, const StatusDownloader& status)
{
    auto time = std::chrono::system_clock::now();
    auto time_ = std::chrono::system_clock::to_time_t(time);
    switch (status.state)
    {
    case State::Done:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "task: " << task << " State: Done" << std::endl;
        break;
    case State::Failed:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "task: " << task << " State: Failed, error: " << status.state_str << std::endl;
        break;
    case State::Redirect:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "task: " << task << " State: Redirect, status: " << status.state_str << std::endl;
        break;
    default:
        break;
    }
}
