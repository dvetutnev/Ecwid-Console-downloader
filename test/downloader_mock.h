#pragma once

#include <gmock/gmock.h>
#include "downloader.h"

class DownloaderMock : public Downloader
{
public:
    MOCK_METHOD2( run, bool(const std::string&, const std::string&) );
    MOCK_METHOD0( stop, void() );
    MOCK_CONST_METHOD0( status, const StatusDownloader&() );
};
