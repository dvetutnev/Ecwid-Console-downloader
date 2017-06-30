#pragma once

#include <gmock/gmock.h>
#include <uvw/emitter.hpp>

struct GetAddrInfoReqMock : public uvw::Emitter<GetAddrInfoReqMock>
{
    MOCK_METHOD1( nodeAddrInfo, void(std::string) );
    MOCK_METHOD0( cancel, bool() );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<GetAddrInfoReqMock>::publish( std::forward<Event>(event) ); }
};
