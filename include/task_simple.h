#pragma once

#include "task.h"
#include <iosfwd>

class TaskListSimple : public TaskList
{
public:

    template< typename String,
              typename = std::enable_if_t< std::is_convertible<String, std::string>::value, String> >
    TaskListSimple(std::istream& stream_, String&& path_) :
        stream{stream_},
        path{ std::forward<String>(path_) }
    {}

    virtual std::shared_ptr<Task> get() override final;

    TaskListSimple() = delete;
    TaskListSimple(const TaskListSimple&) = delete;
    TaskListSimple& operator= (const TaskListSimple&) = delete;
    ~TaskListSimple() = default;

private:
    std::istream& stream;
    const std::string path;
};
