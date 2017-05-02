#pragma once

#include <gmock/gmock.h>
#include <uvw/emitter.hpp>

struct FileReqMock : public uvw::Emitter<FileReqMock>
{
    MOCK_METHOD3( open, void(std::string, int, int) );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<FileReqMock>::publish( std::forward<Event>(event) ); }
};
