#pragma once

#include <string>
#include <utility>
#include <memory>

class Task
{
public:
    Task()
    {}

    template<typename String = std::string>
    Task(String&& uri_, String&& fname_)
        : uri{ std::forward<String>(uri_) },
          fname{ std::forward<String>(fname_) }
    {}

    Task(const Task& other)
        : uri{other.uri},
          fname{other.fname}
    {}

    Task& operator= (const Task& other)
    {
        uri = other.uri;
        fname = other.fname;
        return *this;
    }

    std::string uri;
    std::string fname;
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
