#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "http.h"

#include <algorithm>

using namespace std;

using State = HttpParser::ResponseParseResult::State;

TEST(response_parse, normal)
{
    const string buff_headers = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n";
    const string buff_body = ""
            "Hello world!"
            "World hello!";
    const size_t content_length = 24;

    string body;
    HttpParser::OnData on_data = [&body](unique_ptr<char[]> data, size_t length) { body.append(data.get(), length); };

    auto instance = HttpParser::create(on_data);
    ASSERT_TRUE(instance);

    const string buff = buff_headers + buff_body;
    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Done);
    ASSERT_EQ(result.content_length, content_length);
    ASSERT_EQ(body, buff_body);
}

TEST(response_parse, not_found_404)
{
    const string buff = ""
            "HTTP/1.1 404 Not found\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 10\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Not found!";

    auto instance = HttpParser::create( [](unique_ptr<char[]>, size_t) { FAIL() << "Should not be invoke callback!"; } );
    ASSERT_TRUE(instance);

    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Error);
    cout << "result.err_str => " << result.err_str << endl;
}


TEST(response_parse, redirect_301)
{
    const string buff = ""
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Server: nginx/1.6.2\r\n"
            "Location: http://www.example.org/redirect\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 5\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Moved";
    const string redirect_uri = "http://www.example.org/redirect";

    auto instance = HttpParser::create( [](unique_ptr<char[]>, size_t) { FAIL() << "Should not be invoke callback!"; } );
    ASSERT_TRUE(instance);

    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Redirect);
    ASSERT_EQ(result.redirect_uri, redirect_uri);
}

TEST(response_parse, error_redirect_without_location)
{
    const string buff = ""
            "HTTP/1.1 301 Moved Permanently\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 5\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Moved";

    auto instance = HttpParser::create( [](unique_ptr<char[]>, size_t) { FAIL() << "Should not be invoke callback!"; } );
    ASSERT_TRUE(instance);

    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Error);
    cout << "result.err_str => " << result.err_str << endl;
}

TEST(response_parse, normal_EOF_without_content_length)
{
    const string buff_headers = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n";
    const string buff_body = ""
            "Hello world!"
            "World hello!";

    string body;
    HttpParser::OnData on_data = [&body](unique_ptr<char[]> data, size_t length) { body.append(data.get(), length); };

    auto instance = HttpParser::create(on_data);
    ASSERT_TRUE(instance);

    const string buff = buff_headers + buff_body;
    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);

    const auto result1 = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );
    ASSERT_EQ(result1.state, State::InProgress);

    const auto result2 = instance->response_parse( nullptr, 0 );
    ASSERT_EQ(result2.state, State::Done);
    ASSERT_EQ(body, buff_body);
}

TEST(response_parse, chunked_transfer_encoding)
{
    const string buff = ""
            "HTTP/1.1 200 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "C\r\n"
            "Hello world!\r\n"
            "D\r\n"
            "World, hello!\r\n"
            "0\r\n"
            "\r\n";
    const string buff_body = ""
            "Hello world!"
            "World, hello!";

    string body;
    HttpParser::OnData on_data = [&body](unique_ptr<char[]> data, size_t length) { body.append(data.get(), length); };

    auto instance = HttpParser::create(on_data);
    ASSERT_TRUE(instance);

    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Done);
    ASSERT_EQ(body, buff_body);
}

TEST(response_parse, increment_parse_normal)
{
    const string buff1 = ""
            "HTTP/1.1 20";
    const string buff2 = ""
                         "0 OK\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Len";
    const string buff3 = ""
                         "gth: 24\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Hello wor";
    const string buff4 = ""
                         "ld!"
            "World hello!";

    const string buff_body = ""
            "Hello world!"
            "World hello!";
    const size_t content_length = 24;

    string body;
    HttpParser::OnData on_data = [&body](unique_ptr<char[]> data, size_t length) { body.append(data.get(), length); };

    auto instance = HttpParser::create(on_data);
    ASSERT_TRUE(instance);

    char* const raw_ptr1 = new char[ buff1.size() ];
    copy(begin(buff1), end(buff1), raw_ptr1);
    ASSERT_EQ( instance->response_parse( unique_ptr<char[]>{raw_ptr1}, buff1.size() ).state, State::InProgress );

    char* const raw_ptr2 = new char[ buff2.size() ];
    copy(begin(buff2), end(buff2), raw_ptr2);
    ASSERT_EQ( instance->response_parse( unique_ptr<char[]>{raw_ptr2}, buff2.size() ).state, State::InProgress );

    char* const raw_ptr3 = new char[ buff3.size() ];
    copy(begin(buff3), end(buff3), raw_ptr3);
    ASSERT_EQ( instance->response_parse( unique_ptr<char[]>{raw_ptr3}, buff3.size() ).state, State::InProgress );

    char* const raw_ptr4 = new char[ buff4.size() ];
    copy(begin(buff4), end(buff4), raw_ptr4);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr4}, buff4.size() );
    ASSERT_EQ(result.state, State::Done);
    ASSERT_EQ(result.content_length, content_length);
    ASSERT_EQ(body, buff_body);
}

TEST(response_parse, increment_parse_redirect)
{
    const string buff1 = ""
            "HTTP/1.1 30";
    const string buff2 = ""
                         "1 Moved Permanently\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 5\r\n"
            "Location: http://www.exa";
    const string buff3 = ""
                         "mple.org/redirect\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Moved";
    const string redirect_uri = "http://www.example.org/redirect";

    auto instance = HttpParser::create( [](unique_ptr<char[]>, size_t) { FAIL() << "Should not be invoke callback!"; } );
    ASSERT_TRUE(instance);

    char* const raw_ptr1 = new char[ buff1.size() ];
    copy(begin(buff1), end(buff1), raw_ptr1);
    ASSERT_EQ( instance->response_parse( unique_ptr<char[]>{raw_ptr1}, buff1.size() ).state, State::InProgress );

    char* const raw_ptr2 = new char[ buff2.size() ];
    copy(begin(buff2), end(buff2), raw_ptr2);
    ASSERT_EQ( instance->response_parse( unique_ptr<char[]>{raw_ptr2}, buff2.size() ).state, State::InProgress );

    char* const raw_ptr3 = new char[ buff3.size() ];
    copy(begin(buff3), end(buff3), raw_ptr3);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr3}, buff3.size() );
    ASSERT_EQ(result.state, State::Redirect);
    ASSERT_EQ(result.redirect_uri, redirect_uri);
}

TEST(response_parse, invalid_response)
{
    const string buff = ""
            "HT__TP/1.1 404 Not found\r\n"
            "Server: nginx/1.6.2\r\n"
            "Content-Type: text/pain\r\n"
            "Content-Length: 10\r\n"
            "Connection: keep-alive\r\n"
            "ETag: \"58c2fb69-c\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "\r\n"
            "Not found!";

    auto instance = HttpParser::create( [](unique_ptr<char[]>, size_t) { FAIL() << "Should not be invoke callback!"; } );
    ASSERT_TRUE(instance);

    char* const raw_ptr = new char[ buff.size() ];
    copy(begin(buff), end(buff), raw_ptr);
    const auto result = instance->response_parse( unique_ptr<char[]>{raw_ptr}, buff.size() );

    ASSERT_EQ(result.state, State::Error);
    cout << "result.err_str => " << result.err_str << endl;
}
