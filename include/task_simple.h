#pragma once

#include "task.h"
#include <istream>

class TaskListSimple : public TaskList
{
public:
    TaskListSimple(const TaskListSimple&) = delete;
    TaskListSimple& operator= (const TaskListSimple&) = delete;

    template<typename String = std::string>
    TaskListSimple(std::istream& stream_, String&& path_) :
        stream{stream_},
        path{ std::forward<String>(path_) }
    {}

    virtual std::shared_ptr<Task> get() override;

    virtual ~TaskListSimple() = default;

private:
    std::istream& stream;
    const std::string path;
};
