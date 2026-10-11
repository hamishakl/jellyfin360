// Self-test for src/Json.cpp. Exit code 0 when every check passes.
#include <stdio.h>
#include <string.h>
#include "Json.h"

static int g_failures = 0;
#define CHECK( cond ) do { if( !( cond ) ) { ++g_failures; printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond ); } } while( 0 )

int main()
{
    Json::Value v;
    std::string err;

    CHECK( Json::Parse( "{\"a\":1,\"b\":[true,false,null,\"x\"],\"c\":{\"d\":-2.5e3},\"e\":\"q\\\"\\\\\\n\\u00e9\\ud83c\\udfac\"}", v, err ) );
    CHECK( v.IsObject() );
    CHECK( v["a"].Int() == 1 );
    CHECK( v["b"].Size() == 4 );
    CHECK( v["b"][0].Bool() && !v["b"][1].Bool() && v["b"][2].IsNull() && v["b"][3].Str() == "x" );
    CHECK( v["c"]["d"].Num() == -2500 );
    CHECK( v["e"].Str() == std::string( "q\"\\\n\xc3\xa9\xf0\x9f\x8e\xac" ) );
    CHECK( v["missing"].IsNull() && v["missing"]["deeper"].Str( "dflt" ) == std::string( "dflt" ) );
    CHECK( v["b"][99].IsNull() );
    CHECK( v["a"].Str() == "1" );

    // Jellyfin-sized numbers: RunTimeTicks is 100 ns units.
    CHECK( Json::Parse( "{\"RunTimeTicks\":47520000000,\"Bitrate\":4521130}", v, err ) );
    CHECK( v["RunTimeTicks"].Int64() == 47520000000LL );
    CHECK( v["Bitrate"].Int() == 4521130 );

    CHECK( Json::Parse( "  [ ]  ", v, err ) && v.IsArray() && v.Size() == 0 );
    CHECK( Json::Parse( "\"just a string\"", v, err ) && v.Str() == "just a string" );
    CHECK( !Json::Parse( "{\"a\":}", v, err ) && !err.empty() );
    CHECK( !Json::Parse( "[1,2", v, err ) );
    CHECK( !Json::Parse( "{} x", v, err ) );
    CHECK( !Json::Parse( "", v, err ) );

    // Writer round-trips.
    Json::Value w;
    w.SetObject();
    w.Add( "Username" ).SetString( "ham \"q\" \\ \n" );
    w.Add( "Pw" ).SetString( "" );
    w.Add( "n" ).SetNumber( 42 );
    w.Add( "f" ).SetNumber( 1.5 );
    w.Add( "list" ).SetArray();
    w.Add( "list" ).Push().SetBool( true );
    std::string text = w.Dump();
    CHECK( text == "{\"Username\":\"ham \\\"q\\\" \\\\ \\n\",\"Pw\":\"\",\"n\":42,\"f\":1.5,\"list\":[true]}" );
    Json::Value back;
    CHECK( Json::Parse( text, back, err ) && back["Username"].Str() == w["Username"].Str() && back["f"].Num() == 1.5 );
    CHECK( Json::Quote( "a\x01z" ) == "\"a\\u0001z\"" );

    // Wide conversion for the 360's UTF-16 fonts.
    CHECK( Json::Parse( "{\"Name\":\"Wh\\u0101nau\"}", v, err ) );
    std::wstring wide = v["Name"].WStr();
    CHECK( wide.size() == 6 && wide[2] == 0x101 );

    if( g_failures )
        printf( "%d check(s) failed\n", g_failures );
    else
        printf( "Json: all checks passed\n" );
    return g_failures ? 1 : 0;
}
