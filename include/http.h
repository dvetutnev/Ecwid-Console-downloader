#pragma once

#include <memory>
#include <functional>
#include <string>

extern "C" {
    #include <http_parser.h>
}

class HttpParser
{
public:
    HttpParser() = delete;
    HttpParser(const HttpParser&) = delete;
    HttpParser& operator= (const HttpParser&) = delete;

    struct OnHeadersComplete_Args;
    using OnHeadersComplete = std::function<bool(const OnHeadersComplete_Args&)>;
    using OnBody = std::function<void(const char*, std::size_t)>;
    using OnComplete = std::function<void()>;

public:
    template<typename T1, typename T2, typename T3>
    static std::unique_ptr<HttpParser> create(T1&& on_headers_complete, T2&& on_body, T3&& on_complete)
    {
        return std::unique_ptr<HttpParser>{ new HttpParser(
                        std::forward<T1>(on_headers_complete),
                        std::forward<T2>(on_body),
                        std::forward<T3>(on_complete) )
        };
    }

    class Error
    {
    public:
        explicit Error(enum http_errno code) noexcept
            : m_code{code}
        {}

        explicit operator bool () const noexcept
        {
             return m_code != HPE_OK;
        }

        enum http_errno code() const noexcept
        {
            return m_code;
        }

        const char* str() const noexcept
        {
            return http_errno_description(m_code);
        }

    private:
        enum http_errno m_code;
    };

    struct UriParseResult;
    static std::shared_ptr<UriParseResult> uri_parse(const std::string&);

private:
    template<typename T1, typename T2, typename T3>
    HttpParser(T1&&, T2&&, T3&&)
    {

    }

    http_parser parser;
    http_parser_settings settings;
    struct http_parser_data
    {
        //bool continue_after_headers = false;
        //OnHeadersComplete on_headers_complete;
        //OnBody
    } data;

};

struct HttpParser::UriParseResult
{
    std::string proto;
    std::string host;
    std::size_t port;
    std::string query;
    std::string username;
    std::string password;
};
