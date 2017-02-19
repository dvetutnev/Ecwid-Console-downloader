#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "task.h"

class TaskListMock : public TaskList
{
public:
    MOCK_METHOD0( get, std::shared_ptr<Task>() );
};
