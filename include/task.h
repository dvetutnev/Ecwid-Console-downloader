#pragma once

#include <string>
#include <utility>
#include <memory>

class Task
{
public:
    Task() = default;

    template< typename StringUri,  typename StringFname,
              typename = std::enable_if_t< std::is_convertible<StringUri, std::string>::value, StringUri>,
              typename = std::enable_if_t< std::is_convertible<StringFname, std::string>::value, StringFname> >
    Task(StringUri&& uri_, StringFname&& fname_)
        : uri{ std::forward<StringUri>(uri_) },
          fname{ std::forward<StringFname>(fname_) }
    {}

    Task(const Task&) = default;
    Task& operator= (const Task&) = default;

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
