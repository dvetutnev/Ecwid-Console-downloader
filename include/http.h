#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <type_traits>

extern "C" {
    #include <http_parser.h>
}

class HttpParser
{
public:
    using OnData = std::function<void(std::unique_ptr<char[]>, std::size_t)>;

    template< typename T >
    static
    typename std::enable_if_t< std::is_convertible<T, OnData>::value, std::unique_ptr<HttpParser> >
    create(T&& on_data)
    {
        return std::unique_ptr<HttpParser>{ new HttpParser( std::forward<T>(on_data) ) };
    }

    struct ResponseParseResult
    {
        enum class State { InProgress, Done, Redirect, Error };
        State state;
        std::string redirect_uri;
        std::string err_str;
        std::size_t content_length;
    };

    ResponseParseResult response_parse(std::unique_ptr<char[]>, std::size_t);

    struct UriParseResult;
    static std::unique_ptr<UriParseResult> uri_parse(const std::string&);

private:

    template < typename T>
    explicit HttpParser(T&& on_data)
        : cb_on_data{ std::forward<T>(on_data) }
    {}

    OnData cb_on_data;

public:
    HttpParser() = delete;
    HttpParser(const HttpParser&) = delete;
    HttpParser& operator= (const HttpParser&) = delete;
};

struct HttpParser::UriParseResult
{
    std::string proto;
    std::string host;
    unsigned short port;
    std::string query;
    std::string username;
    std::string password;
};
