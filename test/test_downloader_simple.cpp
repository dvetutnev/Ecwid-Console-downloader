#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <regex>

#include "downloader_simple.h"
#include "aio_loop_mock.h"
#include "aio_dns_mock.h"
#include "aio_tcp_wrapper_mock.h"
#include "aio_timer_mock.h"
#include "aio_file_mock.h"
#include "aio_uvw.h"
#include "on_tick_mock.h"
#include "http.h"

using namespace std;

using ::testing::_;
using ::testing::Return;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::InSequence;
using ::testing::DoAll;
using ::testing::SaveArg;
using ::testing::AtLeast;
using ::testing::AnyNumber;

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
    using ConnectEvent = AIO_UVW::ConnectEvent;
    using WriteEvent = AIO_UVW::WriteEvent;
    using DataEvent = AIO_UVW::DataEvent;
    using EndEvent = AIO_UVW::EndEvent;
    using ShutdownEvent = AIO_UVW::ShutdownEvent;

    using TimerHandle = TimerHandleMock;
    using TimerEvent = AIO_UVW::TimerEvent;

    using FileReq = FileReqMock;
    using FileOpenEvent = AIO_UVW::FileOpenEvent;
    using FileWriteEvent = AIO_UVW::FileWriteEvent;
    using FileCloseEvent = AIO_UVW::FileCloseEvent;

    using FsReq = FsReqMock;
};

/*------- HttpParserMock -------*/

using UriParseResult = HttpParser::UriParseResult;
struct HttpParserMock
{
    using UriParseResult = HttpParser::UriParseResult;
    static HttpParserMock* instance_uri_parse;
    static unique_ptr<UriParseResult> uri_parse(const string& uri) { return instance_uri_parse->uri_parse_(uri); }
    MOCK_METHOD1( uri_parse_, unique_ptr<UriParseResult>(const string&) );

    using OnData = HttpParser::OnData;
    using ResponseParseResult = HttpParser::ResponseParseResult;
    static HttpParserMock* instance_response_parse;
    static unique_ptr<HttpParserMock> create(OnData on_data) { return instance_response_parse->create_( std::move(on_data) ); }
    MOCK_METHOD1( create_, unique_ptr<HttpParserMock>(OnData) );
    ResponseParseResult response_parse(unique_ptr<char[]> data, std::size_t len) { return response_parse_(data.get(), len); }
    MOCK_METHOD2( response_parse_, ResponseParseResult(const char[], std::size_t) );
};
HttpParserMock* HttpParserMock::instance_uri_parse;
HttpParserMock* HttpParserMock::instance_response_parse;

/*------- uri parse -------*/

struct DownloaderSimpleF : public ::testing::Test
{
    DownloaderSimpleF()
        : loop{},
          on_tick{ make_shared<OnTickMock>() },
          instance_uri_parse{ make_unique<HttpParserMock>() },

          backlog{4},
          downloader{ make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick, backlog) }
    {
        HttpParserMock::instance_uri_parse = instance_uri_parse.get();
    }

    virtual ~DownloaderSimpleF()
    {
        EXPECT_TRUE( downloader.unique() );
        EXPECT_LE(on_tick.use_count(), 2);
    }

    LoopMock loop;
    shared_ptr<OnTickMock> on_tick;
    unique_ptr<HttpParserMock> instance_uri_parse;

    const std::size_t backlog;
    shared_ptr<Downloader> downloader;
};

TEST_F(DownloaderSimpleF, uri_parse_failed)
{
    const string bad_uri = "bad_uri";
    const Task task{bad_uri, ""};
    EXPECT_CALL( *instance_uri_parse, uri_parse_(bad_uri) )
            .WillOnce( Return( ByMove(nullptr) ) );
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);

    ASSERT_FALSE( downloader->run(task) );
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- host resolve -------*/

auto on_tick_handler = [](Downloader* d) { cout << "on_tick: status => " << d->status().state_str << endl; };

struct DownloaderSimpleResolve : public DownloaderSimpleF
{
    DownloaderSimpleResolve()
        : host{"www.internet.org"},
          port{8080},
          query{"/uri/fname.zip"},
          task{"www.internet.org", "fname"},
          resolver{ make_shared<GetAddrInfoReqMock>() }
    {
        auto uri_parsed = make_unique<UriParseResult>();
        uri_parsed->host = host;
        uri_parsed->port = port;
        uri_parsed->query = query;
        EXPECT_CALL( *instance_uri_parse, uri_parse_(task.uri) )
                .WillOnce( Return( ByMove( std::move(uri_parsed) ) ) );

        EXPECT_CALL( loop, resource_GetAddrInfoReqMock() )
                .WillOnce( Return(resolver) );
    }

    virtual ~DownloaderSimpleResolve()
    {
        resolver.reset();
        EXPECT_FALSE(resolver);
    }

    const string host;
    const unsigned short port;
    const string query;
    const Task task;
    shared_ptr<GetAddrInfoReqMock> resolver;
};

TEST_F(DownloaderSimpleResolve, host_resolve_failed)
{
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);
    EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
            .Times(1);

    ASSERT_TRUE( downloader->run(task) );

    ASSERT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EAI_NONAME) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResolve, dont_invoke_tick_if_resolver_failed_run)
{
    EXPECT_CALL( *on_tick, invoke_(_) )
            .Times(0);
    EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
            .WillOnce( Invoke( [h = resolver.get()](string) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );

    ASSERT_FALSE( downloader->run(task) );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Failed);
    cout << "status.state_str => " << status.state_str << endl;
    Mock::VerifyAndClearExpectations(instance_uri_parse.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(resolver.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- connect -------*/

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

AIO_UVW::AddrInfoEvent create_addr_info_event_ipv6(const string& ip)
{
    auto addrinfo_raw_ptr = new addrinfo;
    addrinfo_raw_ptr->ai_family = AF_INET6;
    addrinfo_raw_ptr->ai_addrlen = sizeof(sockaddr_in6);
    addrinfo_raw_ptr->ai_addr = reinterpret_cast<sockaddr*>(new sockaddr_in6);
    uv_ip6_addr(ip.c_str(), 0, reinterpret_cast<sockaddr_in6*>(addrinfo_raw_ptr->ai_addr));
    addrinfo_raw_ptr->ai_next = nullptr;
    addrinfo_raw_ptr->ai_canonname = nullptr;
    auto addrinfo_ptr = unique_ptr<addrinfo, void(*)(addrinfo*)>{addrinfo_raw_ptr, [](addrinfo* ptr) { uv_freeaddrinfo(ptr); } };
    return AIO_UVW::AddrInfoEvent{ std::move(addrinfo_ptr) };
}

struct DownloaderSimpleConnect_CreateHandles : public DownloaderSimpleResolve
{
    DownloaderSimpleConnect_CreateHandles()
    {
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);
        EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
                .Times(1);

        EXPECT_TRUE( downloader->run(task) );

        EXPECT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
        Mock::VerifyAndClearExpectations(instance_uri_parse.get());
        Mock::VerifyAndClearExpectations(&loop);
        Mock::VerifyAndClearExpectations(resolver.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }
};

TEST_F(DownloaderSimpleConnect_CreateHandles, socket_create_failed)
{
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillRepeatedly( Return(nullptr) );
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .Times(1)
            .WillRepeatedly( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleConnect_CreateHandles, timer_create_failed)
{
    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(nullptr) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
}

struct DownloaderSimpleConnect : DownloaderSimpleConnect_CreateHandles
{
    DownloaderSimpleConnect()
        : socket{ make_shared<TCPSocketWrapperMock>() },
          timer{ make_shared<TimerHandleMock>() }
    {
        EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
                .WillOnce( Return(socket) );
        EXPECT_CALL( loop, resource_TimerHandleMock() )
                .WillOnce( Return(timer) );
    }

    virtual ~DownloaderSimpleConnect()
    {
        EXPECT_LE(socket.use_count(), 2);
        EXPECT_LE(timer.use_count(), 2);
    }

    shared_ptr<TCPSocketWrapperMock> socket;
    shared_ptr<TimerHandleMock> timer;
};

TEST_F(DownloaderSimpleConnect, connect_failed)
{
    const string ip = "127.0.0.1";
    EXPECT_CALL( *socket, connect(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

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
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect6_failed)
{
    const string ip = "::1";
    EXPECT_CALL( *socket, connect6(ip, port) )
            .Times(1);
    EXPECT_CALL( *timer, start(_,_) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event_ipv6(ip) );

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
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, connect_timeout)
{
    EXPECT_CALL( *socket, connect(_,_) )
            .Times(1);
    TimerHandleMock::Time repeat{0};
    TimerHandleMock::Time timeout{5000};
    EXPECT_CALL( *timer, start(timeout, repeat) )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

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
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

TEST_F(DownloaderSimpleConnect, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, connect(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, start(_,_) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    resolver->publish( create_addr_info_event("127.0.0.1") );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
    ASSERT_EQ(socket.use_count(), 2);
    ASSERT_EQ(timer.use_count(), 2);
}

/*------- http request write -------*/

struct DownloaderSimpleHttpRequest : public DownloaderSimpleConnect
{
    DownloaderSimpleHttpRequest()
        : ip{"127.0.0.1"},
          timeout{5000},
          repeat{0}
    {
        EXPECT_CALL( *socket, connect(ip, port) )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        resolver->publish( create_addr_info_event(ip) );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(&loop);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    const string ip;
    TimerHandleMock::Time timeout;
    TimerHandleMock::Time repeat;
};

TEST_F(DownloaderSimpleHttpRequest, write_failed__and_check_request_data)
{
    string request;
    EXPECT_CALL( *socket, write_(_,_) )
            .WillOnce( Invoke( [&request](const char data[], unsigned int len) { request = string{data, len}; } ) );
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(_,_) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    const string pattern_request_line = "^GET\\s" + query + "\\sHTTP/1.1\\r\\n";
    std::regex re_request_line{pattern_request_line};
    if ( !std::regex_search(request, re_request_line) )
        FAIL() << "Request failed, invalid request line. Request:" << endl << request << endl;

    const string pattern_host_header = "\\r\\nHost:\\s" + host + "\\r\\n";
    std::regex re_host_header{pattern_host_header};
    if ( !std::regex_search(request, re_host_header) )
        FAIL() << "Request failed, invalid Host header. Request:" << endl << request << endl;

    const string pattern_tail = "\\r\\n\\r\\n";
    std::regex re_tail{pattern_tail};
    if ( !std::regex_search(request, re_tail) )
        FAIL() << "Request failed, invalid tail. Request:" << endl << request << endl;

    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleHttpRequest, write_timeout)
{
    EXPECT_CALL( *socket, write_(_,_) )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleHttpRequest, timer_failed_on_run)
{
    {
        InSequence s;
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
    {
        InSequence s;
        EXPECT_CALL( *timer, stop() )
                .Times(1);
        EXPECT_CALL( *timer, start(timeout, repeat) )
                .WillOnce( Invoke( [h = timer.get()](const auto&, const auto&) { h->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ENOSYS) } ); } ) );
        EXPECT_CALL( *timer, close_() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ConnectEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- read start -------*/

struct DownloaderSimpleReadStart : public DownloaderSimpleHttpRequest
{
    DownloaderSimpleReadStart()
    {
        EXPECT_CALL( *socket, write_(_,_) )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::ConnectEvent{} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *socket, read() )
                .Times(1);
        {
            InSequence s;
            EXPECT_CALL( *timer, stop() )
                    .Times(1);
            EXPECT_CALL( *timer, start(timeout, repeat) )
                    .Times(1);
        }
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::WriteEvent{} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }
};

TEST_F(DownloaderSimpleReadStart, unexpectedly_EOF)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleReadStart, error_read)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleReadStart, timeout_read)
{
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    timer->publish( AIO_UVW::TimerEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

/*------- response parse -------*/

struct DownloaderSimpleResponseParse : public DownloaderSimpleReadStart
{
    DownloaderSimpleResponseParse()
        : http_parser{ new HttpParserMock }
    {
        HttpParserMock::instance_response_parse = http_parser;
        EXPECT_CALL( *http_parser, create_(_) )
            .WillOnce( DoAll( SaveArg<0>(&handler_on_data),
                              Return( ByMove( unique_ptr<HttpParserMock>{http_parser} ) ) ) );
    }

    HttpParserMock* http_parser;
    HttpParser::OnData handler_on_data;
};

TEST_F(DownloaderSimpleResponseParse, redirect)
{
    const size_t len = 1510;
    char* data = new char[len];
    const string redirect_uri = "www.internet.org/redirect";
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Redirect;
    result.redirect_uri = redirect_uri;
    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data}, len } );

    const auto status = downloader->status();
    ASSERT_EQ(status.state, StatusDownloader::State::Redirect);
    ASSERT_EQ(status.redirect_uri, redirect_uri);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleResponseParse, error_on_headers)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "404 Not found (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleFileOpen : public DownloaderSimpleResponseParse
{
    DownloaderSimpleFileOpen()
        : file{ make_shared<FileReqMock>() }
    {
        EXPECT_CALL( loop, resource_FileReqMock() )
                .WillOnce( Return(file) );
    }

    virtual ~DownloaderSimpleFileOpen()
    {
        EXPECT_LE(file.use_count(), 2);
    }

    shared_ptr<FileReqMock> file;
};

TEST_F(DownloaderSimpleFileOpen, file_open_error)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    auto data = new char[len];
    auto on_data = [&data, &len, this]() { handler_on_data(unique_ptr<char[]>{data}, len); };
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
            .Times(1);
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST_F(DownloaderSimpleFileOpen, file_open_error_invoke)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    auto data = new char[len];
    auto on_data = [&data, &len, this]() { handler_on_data(unique_ptr<char[]>{data}, len); };
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
            .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EPERM) } ); } ) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{}, 0 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

struct DownloaderSimpleFileWrite : public DownloaderSimpleFileOpen
{
    DownloaderSimpleFileWrite()
        : length_parser{42},
          data_parser{ new char[length_parser] },
          length_file{36},
          data_file{ new char[length_file] },
          fs{ make_shared<FsReqMock>() }
    {
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{data_file}, length_file); };
        EXPECT_CALL( *http_parser, response_parse_(data_parser, length_parser) )
                .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                                  Return(result) ) );
        EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
                .Times(1);
        EXPECT_CALL( *timer, again() )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .WillOnce( Invoke(on_tick_handler) );

        socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data_parser}, length_parser} );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(&loop);
        Mock::VerifyAndClearExpectations(http_parser);
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(timer.get());
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());

        EXPECT_CALL( *file, write(data_file, length_file, 0) )
                .Times(1);
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);

        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );

        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }

    virtual ~DownloaderSimpleFileWrite()
    {
        EXPECT_LE(fs.use_count(), 2);
    }

    const std::size_t length_parser;
    char* const data_parser;
    const std::size_t length_file;
    char* const data_file;

    std::shared_ptr<FsReqMock> fs;
};

TEST_F(DownloaderSimpleFileWrite, read_error)
{
    EXPECT_CALL( *http_parser, response_parse_(_,_) )
            .Times(0);
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNABORTED) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, unexpected_EOF)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Connection lost, unexpected EOF (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(nullptr,_) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::EndEvent{} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, parser_error)
{
    std::size_t len = 42;
    char* const data = new char[len];
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::Error;
    result.err_str = "Response parse failed (HttpParserMock)";
    EXPECT_CALL( *http_parser, response_parse_(data, len) )
            .WillOnce( Return(result) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .WillOnce( Return(true) );
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, file_write_error)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    char* const data_parser = new char[len];
    char* const data_file = new char[len];
    auto on_data = [&data_file, &len, this]() { handler_on_data(unique_ptr<char[]>{data_file}, len); };
    EXPECT_CALL( *http_parser, response_parse_(data_parser, len) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data_parser}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *file, write(data_file, len, length_file) )
            .Times(1);

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), length_file } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(file.get());

    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    {
        InSequence s;
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

TEST_F(DownloaderSimpleFileWrite, file_write_error_invoke)
{
    HttpParser::ResponseParseResult result;
    result.state = HttpParser::ResponseParseResult::State::InProgress;
    std::size_t len = 42;
    char* const data_parser = new char[len];
    char* const data_file = new char[len];
    auto on_data = [&data_file, &len, this]() { handler_on_data(unique_ptr<char[]>{data_file}, len); };
    EXPECT_CALL( *http_parser, response_parse_(data_parser, len) )
            .WillOnce( DoAll( InvokeWithoutArgs(on_data),
                              Return(result) ) );
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{data_parser}, len} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    {
        InSequence s;
        EXPECT_CALL( *file, write(data_file, len, length_file) )
                .WillOnce( InvokeWithoutArgs( [this]() { file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } ); } ) );
        EXPECT_CALL( *file, cancel() )
                .Times(0);
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    file->publish( AIO_UVW::FileWriteEvent{ task.fname.c_str(), length_file } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(on_tick.get());

    EXPECT_CALL( *fs, unlink(task.fname) )
            .Times(1);

    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
}

struct DownloaderSimpleQueue : public DownloaderSimpleFileOpen
{
    DownloaderSimpleQueue()
    {
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::InProgress;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{}, 0); };
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times( AtLeast(1) )
                .WillRepeatedly( DoAll( InvokeWithoutArgs(on_data),
                                        Return(result) ) );
        EXPECT_CALL( *timer, again() )
                .Times( AtLeast(1) );
        EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
                .Times( AtLeast(1) )
                .WillRepeatedly( Invoke(on_tick_handler) );
        EXPECT_CALL( *file, open(task.fname, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP) )
                .Times(1);
    }
};

TEST_F(DownloaderSimpleQueue, socket_stop_on_queue_overflow)
{
    EXPECT_CALL( *socket, stop() )
            .Times(1);

    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(file.get());
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(1);
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECANCELED) } );
}

TEST_F(DownloaderSimpleQueue, socket_read_on_queue_empty)
{
    EXPECT_CALL( *file, write(_,_,_) )
            .Times( static_cast<int>(backlog + 2) );
    EXPECT_CALL( *socket, stop() )
            .Times(1);

    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
    socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
    file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), 42} );
    for (size_t i = 1; i <= backlog; i++)
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );

    Mock::VerifyAndClearExpectations(socket.get());
    // continue read
    EXPECT_CALL( *socket, read() )
            .Times(1);
    for (size_t i = 1; i <= backlog + 1; i++)
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), 42} );
    Mock::VerifyAndClearExpectations(socket.get());
    // Cancel download
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *file, cancel() )
            .Times(0);
    auto fs = make_shared<FsReqMock>();
    EXPECT_CALL( *fs, unlink(_) )
            .Times(0);
    {
        InSequence s;
        EXPECT_CALL( loop, resource_FsReqMock() )
                .WillOnce( Return(fs) );
        EXPECT_CALL( *file, close() )
                .Times(1);
    }
    downloader->stop();
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(&loop);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(file.get());
    // Delete file
    EXPECT_CALL( *fs, unlink(_) )
            .Times(1);
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );

    Mock::VerifyAndClearExpectations(fs.get());
    ASSERT_LE(fs.use_count(), 2);
}

struct DownloaderSimpleComplete : public DownloaderSimpleQueue
{
    DownloaderSimpleComplete()
    {
        EXPECT_CALL( *file, write(_,_,_) )
                .Times( AtLeast(1) );
        // stop read
        EXPECT_CALL( *socket, stop() )
                .Times(1);
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        file->publish( AIO_UVW::FileOpenEvent{ task.fname.c_str() } );
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), 42} );
        for (size_t i = 1; i <= backlog; i++)
            socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(socket.get());
        // continue read
        EXPECT_CALL( *socket, read() )
                .Times(1);
        for (size_t i = 1; i <= backlog + 1; i++)
            file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), 42} );
        Mock::VerifyAndClearExpectations(socket.get());
        // done, close connection
        for (size_t i = 1; i <= backlog - 1; i++)
            socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(http_parser);
        bool timer_stoped = false;
        EXPECT_CALL( *timer, stop() )
                .Times( AnyNumber() )
                .WillRepeatedly( Invoke( [&timer_stoped]() { timer_stoped = true; } ) );
        EXPECT_CALL( *socket, shutdown() )
                .Times(1);
        EXPECT_CALL( *timer, close_() )
                .Times(1);
        HttpParser::ResponseParseResult result;
        result.state = HttpParser::ResponseParseResult::State::Done;
        auto on_data = [this]() { handler_on_data(unique_ptr<char[]>{}, 0); };
        EXPECT_CALL( *http_parser, response_parse_(_,_) )
                .Times(1)
                .WillRepeatedly( DoAll( InvokeWithoutArgs(on_data),
                                        Return(result) ) );
        socket->publish( AIO_UVW::DataEvent{unique_ptr<char[]>{}, 0} );
        Mock::VerifyAndClearExpectations(socket.get());
        Mock::VerifyAndClearExpectations(http_parser);
        if (!timer_stoped)
            timer->publish( AIO_UVW::TimerEvent{} ); // timer should be cleared or stoped
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        // done, close file
        EXPECT_CALL( *file, close() )
                .Times(1);
        EXPECT_CALL( loop, resource_FsReqMock() ) // dont delete file
                .Times(0);
        EXPECT_CALL( *socket, read() )
                .Times(0);
        for (size_t i = 1; i <= backlog; i++)
            file->publish( AIO_UVW::FileWriteEvent{task.fname.c_str(), 42} );
        Mock::VerifyAndClearExpectations(file.get());
        Mock::VerifyAndClearExpectations(&loop);
        Mock::VerifyAndClearExpectations(socket.get());
        EXPECT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
        // close handles
        EXPECT_CALL( *socket, close_() )
                .Times(1);
    }
};

TEST_F(DownloaderSimpleComplete, close_socket_before_file)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_file_before_socket)
{
    file->publish( AIO_UVW::FileCloseEvent{task.fname.c_str()} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Done);
}

TEST_F(DownloaderSimpleComplete, close_socket_before_file_error)
{
    socket->publish( AIO_UVW::ShutdownEvent{} );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);

    EXPECT_CALL( *socket, close_() )
            .Times( AnyNumber() );
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );

    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}

TEST_F(DownloaderSimpleComplete, file_error)
{
    EXPECT_CALL( *timer, close_() )
            .Times( AnyNumber() );
    file->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_EIO) } );
    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
}
