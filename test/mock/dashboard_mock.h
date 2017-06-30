#pragma once

#include <gmock/gmock.h>
#include "dashboard.h"

struct DashboardMock : public Dashboard
{
    MOCK_METHOD2( update, void(std::size_t, const StatusDownloader&) );
};
