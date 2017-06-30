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
    DashboardSimple() = default;
    virtual void update(std::size_t, const StatusDownloader&) override;

    DashboardSimple(const DashboardSimple&) = delete;
    DashboardSimple(DashboardSimple&&) = delete;
    DashboardSimple& operator= (const DashboardSimple&) = delete;
    DashboardSimple& operator= (DashboardSimple&&) = delete;
    virtual ~DashboardSimple() = default;
};

void DashboardSimple::update(std::size_t job_it, const StatusDownloader& status)
{
    auto time = std::chrono::system_clock::now();
    auto time_ = std::chrono::system_clock::to_time_t(time);
    switch (status.state)
    {
    case State::Done:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "#" << job_it << " State: Done" << std::endl;
        break;
    case State::Failed:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "#" << job_it << " State: Failed, error: " << status.state_str << std::endl;
        break;
    case State::Redirect:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "#" << job_it << " State: Redirect, status: " << status.state_str << std::endl;
        break;
    default:
        break;
    }
}
