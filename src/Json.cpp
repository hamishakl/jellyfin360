#include "Platform.h"
#include <stdlib.h>
#include <string.h>
#include "Json.h"
#include "Xml.h"   // Utf8ToWide

namespace
{
    const Json::Value g_null;

    struct Parser
    {
        const std::string& s;
        size_t             pos;
        std::string        error;
        int                depth;

        Parser( const std::string& text ) : s( text ), pos( 0 ), depth( 0 ) {}

        bool Fail( const char* what )
        {
            if( error.empty() )
            {
                char buf[96];
                sprintf_s( buf, "%s at offset %u", what, (unsigned)pos );
                error = buf;
            }
            return false;
        }

        void Ws()
        {
            while( pos < s.size() && ( s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n' ) )
                ++pos;
        }

        bool Hex4( unsigned& out )
        {
            if( pos + 4 > s.size() )
                return false;
            out = 0;
            for( int i = 0; i < 4; ++i )
            {
                char c = s[pos++];
                out <<= 4;
                if( c >= '0' && c <= '9' )      out |= c - '0';
                else if( c >= 'a' && c <= 'f' ) out |= c - 'a' + 10;
                else if( c >= 'A' && c <= 'F' ) out |= c - 'A' + 10;
                else return false;
            }
            return true;
        }

        bool String( std::string& out )
        {
            if( pos >= s.size() || s[pos] != '"' )
                return Fail( "expected string" );
            ++pos;
            while( pos < s.size() )
            {
                char c = s[pos++];
                if( c == '"' )
                    return true;
                if( c != '\\' )
                {
                    out += c;
                    continue;
                }
                if( pos >= s.size() )
                    break;
                char e = s[pos++];
                switch( e )
                {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    unsigned cp;
                    if( !Hex4( cp ) )
                        return Fail( "bad \\u escape" );
                    if( cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u' )
                    {
                        size_t save = pos;
                        pos += 2;
                        unsigned lo;
                        if( Hex4( lo ) && lo >= 0xDC00 && lo <= 0xDFFF )
                            cp = 0x10000 + ( ( cp - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
                        else
                            pos = save;
                    }
                    out += Json::Utf8FromCodepoint( cp );
                    break;
                }
                default:
                    return Fail( "bad escape" );
                }
            }
            return Fail( "unterminated string" );
        }

        bool Number( Json::Value& out )
        {
            size_t start = pos;
            if( pos < s.size() && ( s[pos] == '-' || s[pos] == '+' ) )
                ++pos;
            while( pos < s.size() && ( ( s[pos] >= '0' && s[pos] <= '9' ) || s[pos] == '.' || s[pos] == 'e' ||
                                       s[pos] == 'E' || s[pos] == '-' || s[pos] == '+' ) )
                ++pos;
            if( pos == start )
                return Fail( "expected value" );
            out.SetNumber( atof( s.substr( start, pos - start ).c_str() ) );
            return true;
        }

        bool Literal( const char* word, size_t len )
        {
            if( s.compare( pos, len, word ) != 0 )
                return Fail( "expected value" );
            pos += len;
            return true;
        }

        bool ValueAt( Json::Value& out )
        {
            Ws();
            if( pos >= s.size() )
                return Fail( "unexpected end" );
            if( ++depth > 64 )
                return Fail( "nested too deep" );
            bool ok;
            char c = s[pos];
            if( c == '{' )
            {
                ++pos;
                out.SetObject();
                Ws();
                if( pos < s.size() && s[pos] == '}' )
                {
                    ++pos;
                    ok = true;
                }
                else
                {
                    for( ;; )
                    {
                        Ws();
                        std::string key;
                        if( !String( key ) )
                            return false;
                        Ws();
                        if( pos >= s.size() || s[pos] != ':' )
                            return Fail( "expected ':'" );
                        ++pos;
                        if( !ValueAt( out.Add( key ) ) )
                            return false;
                        Ws();
                        if( pos < s.size() && s[pos] == ',' )
                        {
                            ++pos;
                            continue;
                        }
                        if( pos < s.size() && s[pos] == '}' )
                        {
                            ++pos;
                            break;
                        }
                        return Fail( "expected ',' or '}'" );
                    }
                    ok = true;
                }
            }
            else if( c == '[' )
            {
                ++pos;
                out.SetArray();
                Ws();
                if( pos < s.size() && s[pos] == ']' )
                {
                    ++pos;
                    ok = true;
                }
                else
                {
                    for( ;; )
                    {
                        if( !ValueAt( out.Push() ) )
                            return false;
                        Ws();
                        if( pos < s.size() && s[pos] == ',' )
                        {
                            ++pos;
                            continue;
                        }
                        if( pos < s.size() && s[pos] == ']' )
                        {
                            ++pos;
                            break;
                        }
                        return Fail( "expected ',' or ']'" );
                    }
                    ok = true;
                }
            }
            else if( c == '"' )
            {
                std::string str;
                ok = String( str );
                if( ok )
                    out.SetString( str );
            }
            else if( c == 't' ) { ok = Literal( "true", 4 );  if( ok ) out.SetBool( true ); }
            else if( c == 'f' ) { ok = Literal( "false", 5 ); if( ok ) out.SetBool( false ); }
            else if( c == 'n' ) { ok = Literal( "null", 4 );  if( ok ) out.SetNull(); }
            else
                ok = Number( out );
            --depth;
            return ok;
        }
    };

    void DumpTo( const Json::Value& v, std::string& out )
    {
        switch( v.GetType() )
        {
        case Json::Value::NUL:    out += "null"; break;
        case Json::Value::BOOL:   out += v.Bool() ? "true" : "false"; break;
        case Json::Value::NUMBER:
        {
            char buf[64];
            double d = v.Num();
            if( d == (double)(long long)d && d < 1e18 && d > -1e18 )
                sprintf_s( buf, "%lld", (long long)d );
            else
                sprintf_s( buf, "%.17g", d );
            out += buf;
            break;
        }
        case Json::Value::STRING: out += Json::Quote( v.Str() ); break;
        case Json::Value::ARRAY:
        {
            out += '[';
            const std::vector<Json::Value>& a = v.Elements();
            for( size_t i = 0; i < a.size(); ++i )
            {
                if( i )
                    out += ',';
                DumpTo( a[i], out );
            }
            out += ']';
            break;
        }
        case Json::Value::OBJECT:
        {
            out += '{';
            const std::vector<std::pair<std::string, Json::Value> >& m = v.Members();
            for( size_t i = 0; i < m.size(); ++i )
            {
                if( i )
                    out += ',';
                out += Json::Quote( m[i].first );
                out += ':';
                DumpTo( m[i].second, out );
            }
            out += '}';
            break;
        }
        }
    }
}

std::string Json::Utf8FromCodepoint( unsigned cp )
{
    std::string out;
    if( cp < 0x80 )
        out += (char)cp;
    else if( cp < 0x800 )
    {
        out += (char)( 0xC0 | ( cp >> 6 ) );
        out += (char)( 0x80 | ( cp & 0x3F ) );
    }
    else if( cp < 0x10000 )
    {
        out += (char)( 0xE0 | ( cp >> 12 ) );
        out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
        out += (char)( 0x80 | ( cp & 0x3F ) );
    }
    else
    {
        out += (char)( 0xF0 | ( cp >> 18 ) );
        out += (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
        out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
        out += (char)( 0x80 | ( cp & 0x3F ) );
    }
    return out;
}

void Json::Value::Clear()
{
    m_type = NUL;
    m_bool = false;
    m_num = 0;
    m_str.clear();
    m_arr.clear();
    m_obj.clear();
}

const Json::Value& Json::Value::operator[]( const char* key ) const
{
    if( m_type == OBJECT )
        for( size_t i = 0; i < m_obj.size(); ++i )
            if( m_obj[i].first == key )
                return m_obj[i].second;
    return g_null;
}

const Json::Value& Json::Value::operator[]( size_t index ) const
{
    if( m_type == ARRAY && index < m_arr.size() )
        return m_arr[index];
    return g_null;
}

bool Json::Value::Has( const char* key ) const
{
    if( m_type != OBJECT )
        return false;
    for( size_t i = 0; i < m_obj.size(); ++i )
        if( m_obj[i].first == key )
            return true;
    return false;
}

size_t Json::Value::Size() const
{
    return m_type == ARRAY ? m_arr.size() : m_type == OBJECT ? m_obj.size() : 0;
}

std::string Json::Value::Str( const char* def ) const
{
    if( m_type == STRING )
        return m_str;
    if( m_type == NUMBER )
    {
        char buf[64];
        if( m_num == (double)(long long)m_num )
            sprintf_s( buf, "%lld", (long long)m_num );
        else
            sprintf_s( buf, "%g", m_num );
        return buf;
    }
    if( m_type == BOOL )
        return m_bool ? "true" : "false";
    return def;
}

std::wstring Json::Value::WStr() const
{
    return Xml::Utf8ToWide( Str() );
}

int Json::Value::Int( int def ) const
{
    if( m_type == NUMBER )
        return (int)m_num;
    if( m_type == STRING && !m_str.empty() )
        return atoi( m_str.c_str() );
    if( m_type == BOOL )
        return m_bool ? 1 : 0;
    return def;
}

long long Json::Value::Int64( long long def ) const
{
    if( m_type == NUMBER )
        return (long long)m_num;
    if( m_type == STRING && !m_str.empty() )
        return (long long)atof( m_str.c_str() );
    return def;
}

double Json::Value::Num( double def ) const
{
    if( m_type == NUMBER )
        return m_num;
    if( m_type == STRING && !m_str.empty() )
        return atof( m_str.c_str() );
    return def;
}

bool Json::Value::Bool( bool def ) const
{
    if( m_type == BOOL )
        return m_bool;
    if( m_type == NUMBER )
        return m_num != 0;
    if( m_type == STRING )
        return m_str == "true" || m_str == "1";
    return def;
}

Json::Value& Json::Value::Push()
{
    if( m_type != ARRAY )
        SetArray();
    m_arr.push_back( Value() );
    return m_arr.back();
}

Json::Value& Json::Value::Add( const std::string& key )
{
    if( m_type != OBJECT )
        SetObject();
    for( size_t i = 0; i < m_obj.size(); ++i )
        if( m_obj[i].first == key )
        {
            m_obj[i].second = Value();
            return m_obj[i].second;
        }
    m_obj.push_back( std::make_pair( key, Value() ) );
    return m_obj.back().second;
}

std::string Json::Value::Dump() const
{
    std::string out;
    DumpTo( *this, out );
    return out;
}

bool Json::Parse( const std::string& text, Value& out, std::string& error )
{
    Parser p( text );
    out = Value();
    if( !p.ValueAt( out ) )
    {
        error = p.error;
        return false;
    }
    p.Ws();
    if( p.pos != text.size() )
    {
        p.Fail( "trailing characters" );
        error = p.error;
        return false;
    }
    return true;
}

std::string Json::Quote( const std::string& s )
{
    std::string out( "\"" );
    for( size_t i = 0; i < s.size(); ++i )
    {
        unsigned char c = (unsigned char)s[i];
        switch( c )
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if( c < 0x20 )
            {
                char buf[8];
                sprintf_s( buf, "\\u%04x", c );
                out += buf;
            }
            else
                out += (char)c;
        }
    }
    out += '"';
    return out;
}
