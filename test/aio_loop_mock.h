#pragma once

#include <gmock/gmock.h>
#include <memory>

template< typename Event, typename Resource >
using Callback = std::function< void(const Event&, Resource&) >;

struct LoopMock;
struct GetAddrInfoReqMock;
namespace LoopMock_internal {

template< typename T >
std::shared_ptr<T> resource(LoopMock&) { return nullptr; }

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock&);

}

struct LoopMock
{
    template< typename T >
    std::shared_ptr<T> resource() { return LoopMock_internal::resource<T>(*this); }

    MOCK_METHOD0( resource_GetAddrInfoReqMock, std::shared_ptr<GetAddrInfoReqMock>() );
};

namespace LoopMock_internal {

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock& self) { return self.resource_GetAddrInfoReqMock(); }

}
