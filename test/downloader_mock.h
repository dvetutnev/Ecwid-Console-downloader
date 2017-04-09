#pragma once

#include <gmock/gmock.h>
#include "downloader.h"

class DownloaderMock : public Downloader
{
public:
    MOCK_METHOD1( run, bool(const Task&) );
    MOCK_METHOD0( stop, void() );
    MOCK_CONST_METHOD0( status, const StatusDownloader&() );
};
