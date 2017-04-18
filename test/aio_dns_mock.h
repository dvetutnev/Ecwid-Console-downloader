#pragma once

#include <gmock/gmock.h>
#include <uvw/emitter.hpp>

struct GetAddrInfoReqMock : public uvw::Emitter<GetAddrInfoReqMock>
{
    MOCK_METHOD1( getNodeAddrInfo, void(std::string) );

    template< typename Event >
    void publish(Event&& event) { uvw::Emitter<GetAddrInfoReqMock>::publish( std::forward<Event>(event) ); }
};
