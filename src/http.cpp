#include "http.h"

using namespace std;

/* response parser */

HttpParser::Error HttpParser::response_parse(const char* buf, std::size_t len)
{
    http_parser_execute( &parser, &settings, buf, len );
    return Error{ static_cast<http_errno>(parser.http_errno) };
}

int HttpParser::on_status(http_parser* parser, const char* buf, size_t len)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( !self->cb_on_headers_complete )
        return 0;

    if ( !self->headers_complete_args )
        self->headers_complete_args = make_unique<OnHeadersComplete_Args>();

    self->headers_complete_args->http_code = parser->status_code;
    self->headers_complete_args->http_reason.append(buf, len);
    return 0;
}

int HttpParser::on_header_field(http_parser* parser, const char* buf, size_t len)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( !self->cb_on_headers_complete )
        return 0;

    if ( self->mode_header == ModeHeader::Filed )
    {
        self->field_header.append(buf, len);
    } else if ( self->mode_header == ModeHeader::Value )
    {
        self->headers_complete_args->headers[ std::move(self->field_header) ] = std::move(self->value_header);
        self->field_header = string{buf, len};
        self->mode_header = ModeHeader::Filed;
    }
    return 0;
}

int HttpParser::on_header_value(http_parser* parser, const char* buf, size_t len)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( !self->cb_on_headers_complete )
        return 0;

    if ( self->mode_header == ModeHeader::Value )
    {
        self->value_header.append(buf, len);
    } else if ( self->mode_header == ModeHeader::Filed )
    {
        self->value_header = string{buf, len};
        self->mode_header = ModeHeader::Value;
    }
    return 0;
}

int HttpParser::on_headers_complete(http_parser* parser)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( !self->cb_on_headers_complete )
        return 0;

    self->headers_complete_args->headers[ std::move(self->field_header) ] = std::move(self->value_header);
    self->headers_complete_args->content_length = parser->content_length;
    bool is_continue = self->cb_on_headers_complete( std::move(self->headers_complete_args) );
    if ( !is_continue )
        http_parser_pause(parser, 1);

    return 0;
}

int HttpParser::on_body(http_parser* parser, const char* buf, size_t len)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( self->cb_on_headers_complete && self->cb_on_body )
        self->cb_on_body(buf, len);
    return 0;
}

int HttpParser::on_complete(http_parser* parser)
{
    auto self = reinterpret_cast<HttpParser*>(parser->data);
    if ( self->cb_on_headers_complete && self->cb_on_complete )
        self->cb_on_complete();
    http_parser_pause(parser, 1);
    return 0;
}

/* result response parser */

HttpParser::Error::Error(enum http_errno code) noexcept
    : m_code{code}
{}

HttpParser::Error& HttpParser::Error::operator= (const Error& other) noexcept
{
    this->m_code = other.m_code;
    return *this;
}

HttpParser::Error::operator bool () const noexcept
{
     return !(m_code == HPE_OK || m_code == HPE_PAUSED);
}

enum http_errno HttpParser::Error::code() const noexcept
{
    return m_code;
}

const char* HttpParser::Error::str() const noexcept
{
    return http_errno_description(m_code);
}

/* uri parser */

static const map<string, size_t> proto_default_port{
    { "http", 80u },
    { "https", 443u }
};

shared_ptr<HttpParser::UriParseResult> HttpParser::uri_parse(const string& uri)
{
    shared_ptr<UriParseResult> ret{};
    struct http_parser_url result;
    http_parser_url_init(&result);

    if ( http_parser_parse_url( uri.data(), uri.size(), 0, &result ) == 0 )
    {
        const string& proto = uri.substr( result.field_data[UF_SCHEMA].off, result.field_data[UF_SCHEMA].len );
        if ( proto_default_port.find(proto) == proto_default_port.end() )
            return ret;

        ret = make_shared<UriParseResult>();

        ret->proto = proto;
        ret->host = uri.substr( result.field_data[UF_HOST].off, result.field_data[UF_HOST].len );

        ret->port = ( result.field_set & (1 << UF_PORT) ) ? result.port : proto_default_port.at( ret->proto );

        ret->query = ( result.field_set & (1 << UF_PATH) ) ? uri.substr( result.field_data[UF_PATH].off, result.field_data[UF_PATH].len ) : "/";
        if ( result.field_set & (1 << UF_QUERY) )
            ret->query += "?" + uri.substr( result.field_data[UF_QUERY].off, result.field_data[UF_QUERY].len );
        if ( result.field_set & (1 << UF_FRAGMENT) )
            ret->query += "#" + uri.substr( result.field_data[UF_FRAGMENT].off, result.field_data[UF_FRAGMENT].len );

        if ( result.field_set & (1 << UF_USERINFO) )
        {
            const string& userinfo = uri.substr( result.field_data[UF_USERINFO].off, result.field_data[UF_USERINFO].len );
            auto pos = userinfo.find(":");
            ret->username = userinfo.substr(0, pos);
            if ( pos != userinfo.npos )
                ret->password = userinfo.substr(pos + 1);
        }
    }
    return ret;
}
