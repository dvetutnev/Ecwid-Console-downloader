#pragma once

#include <gmock/gmock.h>
#include "task.h"

class TaskListMock : public TaskList
{
public:
    MOCK_METHOD0( get, std::unique_ptr<Task>() );
};
