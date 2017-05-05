#pragma once

#include <gmock/gmock.h>
#include <uvw/emitter.hpp>

struct FileReqMock : public uvw::Emitter<FileReqMock>
{
    MOCK_METHOD3( open, void(std::string, int, int) );
    MOCK_METHOD3( write, void(const char*, std::size_t, std::size_t) );
    MOCK_METHOD0( close, void() );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<FileReqMock>::publish( std::forward<Event>(event) ); }
};

struct FsReqMock : public uvw::Emitter<FsReqMock>
{
    MOCK_METHOD1( unlink, void(std::string) );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<FsReqMock>::publish( std::forward<Event>(event) ); }
};
