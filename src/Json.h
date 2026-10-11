#pragma once
#include <string>
#include <vector>
#include <utility>

// Small JSON reader/writer for Jellyfin replies (C++03, no exceptions: a missing key reads as null).
namespace Json
{
    class Value
    {
    public:
        enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

        Value() : m_type( NUL ), m_bool( false ), m_num( 0 ) {}

        Type   GetType() const  { return m_type; }
        bool   IsNull() const   { return m_type == NUL; }
        bool   IsObject() const { return m_type == OBJECT; }
        bool   IsArray() const  { return m_type == ARRAY; }
        bool   IsString() const { return m_type == STRING; }
        bool   IsNumber() const { return m_type == NUMBER; }

        // Object member by name / array element by index; a shared null Value when absent.
        const Value& operator[]( const char* key ) const;
        const Value& operator[]( size_t index ) const;
        const Value& operator[]( int index ) const { return ( *this )[(size_t)( index < 0 ? ~(size_t)0 : index )]; }
        bool         Has( const char* key ) const;
        size_t       Size() const;      // members or elements, 0 otherwise

        // Conversions with a default for null / wrong type. Numbers in strings are accepted.
        std::string  Str( const char* def = "" ) const;
        std::wstring WStr() const;      // UTF-8 -> wide
        int          Int( int def = 0 ) const;
        long long    Int64( long long def = 0 ) const;
        double       Num( double def = 0 ) const;
        bool         Bool( bool def = false ) const;

        // Builders (also used by the parser).
        void SetNull()                         { Clear(); }
        void SetBool( bool b )                 { Clear(); m_type = BOOL; m_bool = b; }
        void SetNumber( double d )             { Clear(); m_type = NUMBER; m_num = d; }
        void SetString( const std::string& s ) { Clear(); m_type = STRING; m_str = s; }
        void SetArray()                        { Clear(); m_type = ARRAY; }
        void SetObject()                       { Clear(); m_type = OBJECT; }
        Value& Push();                         // appends a null element (array)
        Value& Add( const std::string& key );  // adds/replaces a member (object)

        const std::vector<Value>& Elements() const { return m_arr; }
        const std::vector<std::pair<std::string, Value> >& Members() const { return m_obj; }

        std::string Dump() const;              // compact JSON text

    private:
        void Clear();

        Type                                         m_type;
        bool                                         m_bool;
        double                                       m_num;
        std::string                                  m_str;
        std::vector<Value>                           m_arr;
        std::vector<std::pair<std::string, Value> >  m_obj;
    };

    // Parses a whole document. On failure error says where.
    bool Parse( const std::string& text, Value& out, std::string& error );

    // "text" with JSON escaping, quotes included.
    std::string Quote( const std::string& s );

    std::string Utf8FromCodepoint( unsigned cp );
}
