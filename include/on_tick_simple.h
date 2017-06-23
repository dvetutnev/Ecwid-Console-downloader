#pragma once

#include "on_tick.h"
#include "factory.h"
#include "job.h"
#include "dashboard.h"

#include <list>

class OnTickSimple : public OnTick
{
public:
    using JobContainer = std::list<Job>;
    OnTickSimple(JobContainer& job_container_, std::weak_ptr<Factory> factory, TaskList& task_list_, Dashboard& dashboard_, std::size_t max_redirect_ = 10)
        : job_container{job_container_},
          weak_factory{factory},
          task_list{task_list_},
          dashboard{dashboard_},
          max_redirect{max_redirect_}
    {}

    virtual void invoke(std::shared_ptr<Downloader>) override;

    OnTickSimple() = delete;
    OnTickSimple(const OnTickSimple&) = delete;
    OnTickSimple& operator= (const OnTickSimple) = delete;
    virtual ~OnTickSimple() = default;

private:
    void next_task(const JobContainer::iterator);
    void redirect(JobContainer::iterator);
    JobContainer::iterator find_job(Downloader*);

    JobContainer& job_container;
    std::weak_ptr<Factory> weak_factory;
    TaskList& task_list;
    Dashboard& dashboard;
    const std::size_t max_redirect;
};
