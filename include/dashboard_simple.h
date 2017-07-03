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
    DashboardSimple()
        : done_tasks{0},
          total_downloaded{0}
    {}

    virtual void update(std::size_t, const StatusDownloader&) override;
    std::pair<std::size_t, std::size_t> status() { return std::pair<std::size_t, std::size_t>{done_tasks, total_downloaded}; }

    DashboardSimple(const DashboardSimple&) = delete;
    DashboardSimple(DashboardSimple&&) = delete;
    DashboardSimple& operator= (const DashboardSimple&) = delete;
    DashboardSimple& operator= (DashboardSimple&&) = delete;

    virtual ~DashboardSimple() = default;

private:
    std::size_t done_tasks;
    std::size_t total_downloaded;
};

void DashboardSimple::update(std::size_t job_it, const StatusDownloader& status)
{
    auto time = std::chrono::system_clock::now();
    auto time_ = std::chrono::system_clock::to_time_t(time);
    switch (status.state)
    {
    case State::Done:
        std::cout << std::put_time(std::localtime(&time_), "%H:%M:%S  ") << "#" << job_it << " State: Done" << std::endl;
        done_tasks++;
        total_downloaded += status.downloaded;
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
