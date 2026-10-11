# Native build of the server-facing half of Jellyfin 360 for testing on a Mac/PC.
#   cmake -S tools/mac -B build/mac && cmake --build build/mac
#   build/mac/jsontest && build/mac/jftool info http://server:8096
cmake_minimum_required(VERSION 3.16)
project(jellyfin360-tools CXX)
set(CMAKE_CXX_STANDARD 11)
find_package(CURL REQUIRED)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_library(jfcore STATIC ${SRC}/Json.cpp ${SRC}/Xml.cpp ${SRC}/JellyfinApi.cpp ${SRC}/JellyfinAuth.cpp
                          HttpCurl.cpp LogStdio.cpp)
target_include_directories(jfcore PUBLIC ${SRC})
target_link_libraries(jfcore PUBLIC CURL::libcurl)
target_compile_options(jfcore PRIVATE -Wall -Wno-unused-function)

add_executable(jftool jftool.cpp)
target_link_libraries(jftool jfcore)

add_executable(jsontest JsonTest.cpp)
target_link_libraries(jsontest jfcore)
enable_testing()
add_test(NAME json COMMAND jsontest)
