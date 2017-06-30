#pragma once

#include <gmock/gmock.h>
#include <memory>

struct LoopMock;
struct GetAddrInfoReqMock;
struct TcpHandleMock;
struct TimerHandleMock;
struct FileReqMock;
struct FsReqMock;
struct TCPSocketMock;

namespace LoopMock_internal {

template< typename T >
std::shared_ptr<T> resource(LoopMock&) { return nullptr; }

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock&);

template<>
std::shared_ptr<TcpHandleMock> resource<TcpHandleMock>(LoopMock&);

template<>
std::shared_ptr<TimerHandleMock> resource<TimerHandleMock>(LoopMock&);

template<>
std::shared_ptr<FileReqMock> resource<FileReqMock>(LoopMock&);

template<>
std::shared_ptr<FsReqMock> resource<FsReqMock>(LoopMock&);

template<>
std::shared_ptr<TCPSocketMock> resource<TCPSocketMock>(LoopMock&);

}

struct LoopMock
{
    template< typename T >
    std::shared_ptr<T> resource() { return LoopMock_internal::resource<T>(*this); }

    MOCK_METHOD0( resource_GetAddrInfoReqMock, std::shared_ptr<GetAddrInfoReqMock>() );
    MOCK_METHOD0( resource_TcpHandleMock, std::shared_ptr<TcpHandleMock>() );
    MOCK_METHOD0( resource_TimerHandleMock, std::shared_ptr<TimerHandleMock>() );
    MOCK_METHOD0( resource_FileReqMock, std::shared_ptr<FileReqMock>() );
    MOCK_METHOD0( resource_FsReqMock, std::shared_ptr<FsReqMock>() );
    MOCK_METHOD0( resource_TCPSocketMock, std::shared_ptr<TCPSocketMock>() );
};

namespace LoopMock_internal {

template<>
std::shared_ptr<GetAddrInfoReqMock> resource<GetAddrInfoReqMock>(LoopMock& self) { return self.resource_GetAddrInfoReqMock(); }

template<>
std::shared_ptr<TcpHandleMock> resource<TcpHandleMock>(LoopMock& self) { return self.resource_TcpHandleMock(); }

template<>
std::shared_ptr<TimerHandleMock> resource<TimerHandleMock>(LoopMock& self) { return self.resource_TimerHandleMock(); }

template<>
std::shared_ptr<FileReqMock> resource<FileReqMock>(LoopMock& self) { return self.resource_FileReqMock(); }

template<>
std::shared_ptr<FsReqMock> resource<FsReqMock>(LoopMock& self) { return self.resource_FsReqMock(); }

template<>
std::shared_ptr<TCPSocketMock> resource<TCPSocketMock>(LoopMock& self) { return self.resource_TCPSocketMock(); }
}
