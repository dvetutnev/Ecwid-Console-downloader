#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <regex>

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
using ::testing::Return;
using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Invoke;
using ::testing::InSequence;

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

          downloader{ make_shared< DownloaderSimple<AIO_Mock, HttpParserMock> >(loop, on_tick) }
    {
        HttpParserMock::instance_uri_parse = instance_uri_parse.get();
    }

    virtual ~DownloaderSimpleF()
    {
        EXPECT_TRUE( downloader.unique() );
        EXPECT_EQ(on_tick.use_count(), 2);
    }

    LoopMock loop;
    shared_ptr<OnTickMock> on_tick;
    unique_ptr<HttpParserMock> instance_uri_parse;

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
          resolver{ make_shared<GetAddrInfoReqMock>() }
    {
        auto uri_parsed = make_unique<UriParseResult>();
        uri_parsed->host = host;
        uri_parsed->port = port;
        uri_parsed->query = query;
        EXPECT_CALL( *instance_uri_parse, uri_parse_(_) )
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
    shared_ptr<GetAddrInfoReqMock> resolver;
};

TEST_F(DownloaderSimpleResolve, host_resolve_failed)
{
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

    ASSERT_FALSE( downloader->run( Task{} ) );

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

struct DownloaderSimpleConnect : public DownloaderSimpleResolve
{
    DownloaderSimpleConnect()
    {
        EXPECT_CALL( *on_tick, invoke_(_) )
                .Times(0);
        EXPECT_CALL( *resolver, getNodeAddrInfo(host) )
                .Times(1);

        EXPECT_TRUE( downloader->run( Task{} ) );

        EXPECT_EQ( downloader->status().state, StatusDownloader::State::OnTheGo );
        Mock::VerifyAndClearExpectations(instance_uri_parse.get());
        Mock::VerifyAndClearExpectations(&loop);
        Mock::VerifyAndClearExpectations(resolver.get());
        Mock::VerifyAndClearExpectations(on_tick.get());
    }
};

TEST_F(DownloaderSimpleConnect, socket_create_failed)
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

TEST_F(DownloaderSimpleConnect, socket_timer_failed)
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

TEST_F(DownloaderSimpleConnect, connect_failed)
{
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
    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    auto timer = make_shared<TimerHandleMock>();
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(timer) );

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
    auto socket = make_shared<TCPSocketWrapperMock>();
    EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
            .WillOnce( Return(socket) );
    auto timer = make_shared<TimerHandleMock>();
    EXPECT_CALL( loop, resource_TimerHandleMock() )
            .WillOnce( Return(timer) );

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
          repeat{0},
          socket{ make_shared<TCPSocketWrapperMock>() },
          timer{ make_shared<TimerHandleMock>() }
    {
        EXPECT_CALL( loop, resource_TCPSocketWrapperMock() )
                .WillOnce( Return(socket) );
        EXPECT_CALL( loop, resource_TimerHandleMock() )
                .WillOnce( Return(timer) );
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

    virtual ~DownloaderSimpleHttpRequest()
    {
        EXPECT_EQ(socket.use_count(), 2);
        EXPECT_EQ(timer.use_count(), 2);
    }

    const string ip;
    TimerHandleMock::Time timeout;
    TimerHandleMock::Time repeat;
    shared_ptr<TCPSocketWrapperMock> socket;
    shared_ptr<TimerHandleMock> timer;
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

    socket->publish( AIO_UVW::ErrorEvent{ static_cast<int>(UV_ECONNREFUSED) } );

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

struct DownloaderSimpleResponseRead : public DownloaderSimpleHttpRequest
{
    DownloaderSimpleResponseRead()
        : http_parser{ new HttpParserMock }
    {
        HttpParserMock::instance_response_parse = http_parser;

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

        EXPECT_CALL( *http_parser, create_(_) )
                .WillOnce( Return( ByMove( unique_ptr<HttpParserMock>{http_parser} ) ) );
    }

    HttpParserMock* http_parser;
};

TEST_F(DownloaderSimpleResponseRead, response_parse_error)
{
    const size_t len1 = 42;
    char* data1 = new char[len1];
    HttpParser::ResponseParseResult result1;
    result1.state = HttpParser::ResponseParseResult::State::InProgress;
    EXPECT_CALL( *http_parser, response_parse_(data1, len1) )
            .WillOnce( Return(result1) );
    EXPECT_CALL( *timer, again() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data1}, len1 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::OnTheGo);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());

    const size_t len2 = 1510;
    char* data2 = new char[len2];
    HttpParser::ResponseParseResult result2;
    result2.state = HttpParser::ResponseParseResult::State::Error;
    result2.err_str = "Error from HttpParserMock";
    EXPECT_CALL( *http_parser, response_parse_(data2, len2) )
            .WillOnce( Return(result2) );
    EXPECT_CALL( *socket, close_() )
            .Times(1);
    EXPECT_CALL( *timer, close_() )
            .Times(1);
    EXPECT_CALL( *on_tick, invoke_( downloader.get() ) )
            .WillOnce( Invoke(on_tick_handler) );

    socket->publish( AIO_UVW::DataEvent{ unique_ptr<char[]>{data2}, len2 } );

    ASSERT_EQ(downloader->status().state, StatusDownloader::State::Failed);
    Mock::VerifyAndClearExpectations(http_parser);
    Mock::VerifyAndClearExpectations(socket.get());
    Mock::VerifyAndClearExpectations(timer.get());
    Mock::VerifyAndClearExpectations(on_tick.get());
}

TEST(DownloaderSimple, redirect)
{

}

TEST(DownloaderSimple, on_read_error)
{

}

TEST(DownloaderSimple, unexpect_EOF)
{

}

