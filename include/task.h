#pragma once

#include <string>
#include <utility>
#include <memory>

class Task
{
public:
    template<typename String = std::string>
    Task(String&& uri_, String&& fname_) :
        uri{ std::forward<String>(uri_) },
        fname{ std::forward<String>(fname_) }
    {}

    const std::string uri;
    const std::string fname;
};

class TaskList
{
public:
    virtual std::shared_ptr<Task> get() = 0;
    virtual ~TaskList() = default;
};

inline bool operator== (const Task& a, const Task& b)
{
    return a.uri == b.uri && a.fname == b.fname;
}
