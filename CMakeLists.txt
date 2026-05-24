cmake_minimum_required(VERSION 3.16)
project(voice-ime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /EHsc /W4)
else()
    add_compile_options(-Wall -Wextra -O2 -pthread)
endif()

find_package(Threads REQUIRED)

# whisper.cpp
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

# voice_server
add_executable(voice_server
    server/main.cpp
    server/thread_pool.cpp
    server/session.cpp
    server/asr_worker.cpp
    server/websocket_server.cpp
)

target_include_directories(voice_server PRIVATE
    ${CMAKE_SOURCE_DIR}/server
    ${CMAKE_SOURCE_DIR}/third_party/websocketpp
    ${CMAKE_SOURCE_DIR}/third_party/asio/asio/include
    ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/ggml/include
)

target_compile_definitions(voice_server PRIVATE
    ASIO_STANDALONE
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    _WEBSOCKETPP_CPP11_STL_
    _WEBSOCKETPP_CPP11_FUNCTIONAL_
    _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
    _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
    _WEBSOCKETPP_CPP11_MEMORY_
    _WEBSOCKETPP_CPP11_TYPE_TRAITS_
)

target_link_libraries(voice_server PRIVATE whisper Threads::Threads)

if(MSVC)
    target_link_libraries(voice_server PRIVATE ws2_32)
endif()

install(TARGETS voice_server RUNTIME DESTINATION bin)
