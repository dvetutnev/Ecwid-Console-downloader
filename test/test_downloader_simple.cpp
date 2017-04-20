#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "downloader_simple.h"
#include "aio_loop_mock.h"
#include "aio_dns_mock.h"
#include "aio_tcp_wrapper_mock.h"
#include "aio_timer_mock.h"
#include "aio_uvw.h"
#include "on_tick_mock.h"
#include "http.h"

using namespace std;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Invoke;

struct AIO_Mock
{
    using Loop = LoopMock;
    using ErrorEvent = AIO_UVW::ErrorEvent;

    using AddrInfoEvent = AIO_UVW::AddrInfoEvent;
    using GetAddrInfoReq = GetAddrInfoReqMock;
    using IPAddress = AIO_UVW::IPAddress;
    static auto addrinfo2IPAddress(const addrinfo* addr) { return AIO_UVW::addrinfo2IPAddress(addr); }

    using TCPSocketWrapper = uvw::TCPSocketWrapper;
    using TCPSocketWrapperSimple = TCPSocketWrapperMock;

    using TimerHandle = TimerHandleMock;
    using TimerEvent = AIO_UVW::TimerEvent;
};

/*------- HttpParserMock -------*/
using UriParseResult = HttpParser::UriParseResult;
struct HttpParserMock
{
    using UriParseResult = HttpParser::UriParseResult;
    static HttpParserMock* instance_uri_parse;
    static unique_ptr<UriParseResult> uri_parse(const string& uri) { return instance_uri_parse->uri_parse_(uri); }
    MOCK_METHOD1(uri_parse_, unique_ptr<UriParseResult>(const string&));
};
HttpParserMock* HttpParserMock::instance_uri_parse;

/*------- tests implementation -------*/

TEST(DownloaderSimple, uri_parse_falied)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    const string bad_uri = "bad_uri";
    const Task task{bad_uri, ""};
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove(nullptr) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    ASSERT_FALSE( downloader->run(task) );

    Mock::VerifyAndClearExpectations(on_tick.get());

    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
}

auto on_tick_handler = [](Downloader* d) { cout << "on_tick: status => " << d->status().state_str << endl; };

TEST(DownloaderSimple, host_resolve_failed)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    const string host = "www.internet.org";
    auto uri_parse_result = make_unique<UriParseResult>();
    uri_parse_result->host = host;

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( std::move(uri_parse_result) ) ) );
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);
    EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
            .Times(1);

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EAI_NONAME) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
}

TEST(DownloaderSimple, dont_invoke_tick_if_resolver_failed_run)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>() ) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .WillOnce( Invoke( [&resolver](string) { resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    ASSERT_FALSE( downloader->run( Task{} ) );
    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "status.state_str => " << status.state_str << endl;

    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
}

AIO_UVW::AddrInfoEvent create_addr_info_event(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in);
    uv_ip4_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

TEST(DownloaderSimple, create_socket_failed)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>() ) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .Times(1);

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
}

TEST(DownloaderSimple, create_net_timer_failed)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>() ) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .Times(1);

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(nullptr) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);

    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
    ASSERT_EQ(socket.use_count(), 2);
}

TEST(DownloaderSimple, connect_failed)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    const unsigned short port = 8080;
    auto uri_parsed = make_unique<UriParseResult>();
    uri_parsed->port = port;
    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( std::move(uri_parsed) ) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .Times(1);

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    auto timer = make_shared<TimerHandleMock>();
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(timer) );

    const string ip = "127.0.0.1";
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event(ip) );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST(DownloaderSimple, connect_timeout)
{
    LoopMock loop;
    auto on_tick = make_shared<OnTickMock>();
    auto instance_uri_parse = make_shared<HttpParserMock>();
    HttpParserMock::instance_uri_parse = instance_uri_parse.get();

    auto downloader = make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick);

    EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Return( ByMove( make_unique<UriParseResult>() ) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    auto resolver = make_shared<GetAddrInfoReqMock>();
    EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
            .WillOnce( Return(resolver) );
    EXPECT_CALL( *resolver, getNodeAddrInfo(_) )
            .Times(1);

    ASSERT_TRUE( downloader->run( Task{} ) );
    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    auto timer = make_shared<TimerHandleMock>();
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(timer) );

    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    TimerHandleMock::Time repeat{0};
    TimerHandleMock::Time timeout{5000};
    EXPECT_CALL( *timer, start(timeout, repeat) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times( AtLeast(1) )
            .WillRepeatedly( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    resolver.reset();
    ASSERT_FALSE(resolver);
    ASSERT_TRUE( downloader.unique() );
    ASSERT_EQ(on_tick.use_count(), 2);
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST(DownloaderSimple, net_timer_failed_on_run)
{
// и ErrorEvent от сокета (при вызове его close)
}

