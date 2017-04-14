#pragma once

#include <gmock/gmock.h>
#include <memory>

template< typename Event, typename Resource >
using Callback = std::function< void(Event&, Resource&) >;

struct LoopMock;
struct GetAddrInfoReqMock;
struct TcpHandleMock;

namespace LoopMock_internal {

template< typename T >
std::shared_ptr<T> resource(LoopMock&) { return nullptr; }

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock&);

template<>
std::shared_ptr<TcpHandleMock> resource<TcpHandleMock>(LoopMock&);

}

struct LoopMock
{
    template< typename T >
    std::shared_ptr<T> resource() { return LoopMock_internal::resource<T>(*this); }

    MOCK_METHOD0( resource_GetAddrInfoReqMock, std::shared_ptr<GetAddrInfoReqMock>() );
    MOCK_METHOD0( resource_TcpHandleMock, std::shared_ptr<TcpHandleMock>() );
};

namespace LoopMock_internal {

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock& self) { return self.resource_GetAddrInfoReqMock(); }

template<>
std::shared_ptr<TcpHandleMock> resource<TcpHandleMock>(LoopMock& self) { return self.resource_TcpHandleMock(); }

}
