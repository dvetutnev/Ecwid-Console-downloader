#pragma once

#include <string>
#include <utility>
#include <memory>

class Task
{
public:
    template< typename StringUri,  typename StringFname,
              typename = std::enable_if_t< std::is_convertible<StringUri, std::string>::value, StringUri>,
              typename = std::enable_if_t< std::is_convertible<StringFname, std::string>::value, StringFname> >
    Task(StringUri&& uri_, StringFname&& fname_)
        : uri{ std::forward<StringUri>(uri_) },
          fname{ std::forward<StringFname>(fname_) }
    {}

    const std::string uri;
    const std::string fname;

    Task() = delete;
    Task(const Task&) = delete;
    Task& operator= (const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator= (Task&&) = delete;

    ~Task() = default;
};

class TaskList
{
public:
    virtual std::unique_ptr<Task> get() = 0;
    virtual ~TaskList() = default;
};
