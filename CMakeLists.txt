cmake_minimum_required(VERSION 3.16)

# 抑制 OpenCC / whisper.cpp 等依赖在配置阶段的 CMake 策略与弃用 API 警告
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)
endif()

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
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# OpenCC（繁体→简体）
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_GTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    opencc
    GIT_REPOSITORY https://github.com/BYVoid/OpenCC.git
    GIT_TAG ver.1.1.7
)
FetchContent_MakeAvailable(opencc)

# whisper.cpp
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

# voice_server
add_executable(voice_server
    server/main.cpp
    server/thread_pool.cpp
    server/session.cpp
    server/audio_vad.cpp
    server/asr_worker.cpp
    server/text_convert.cpp
    server/websocket_server.cpp
)

target_include_directories(voice_server PRIVATE
    ${CMAKE_SOURCE_DIR}/server
    ${CMAKE_SOURCE_DIR}/third_party/websocketpp
    ${CMAKE_SOURCE_DIR}/third_party/asio/asio/include
    ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/ggml/include
    ${opencc_SOURCE_DIR}/src
    ${opencc_BINARY_DIR}/src
)

target_compile_definitions(voice_server PRIVATE
    Opencc_BUILT_AS_STATIC
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

target_link_libraries(voice_server PRIVATE whisper libopencc Threads::Threads)

if(MSVC)
    target_link_libraries(voice_server PRIVATE ws2_32)
endif()

# 将 t2s 配置与字典复制到可执行文件旁（opencc/t2s.json 与同目录 .ocd2）
set(VOICE_OPENCC_DATA_DIR "${opencc_BINARY_DIR}/data")
add_custom_command(
    TARGET voice_server
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:voice_server>/opencc"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${opencc_SOURCE_DIR}/data/config/t2s.json"
        "$<TARGET_FILE_DIR:voice_server>/opencc/t2s.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VOICE_OPENCC_DATA_DIR}/TSPhrases.ocd2"
        "$<TARGET_FILE_DIR:voice_server>/opencc/TSPhrases.ocd2"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VOICE_OPENCC_DATA_DIR}/TSCharacters.ocd2"
        "$<TARGET_FILE_DIR:voice_server>/opencc/TSCharacters.ocd2"
    COMMENT "Copy OpenCC t2s data next to voice_server"
)
add_dependencies(voice_server Dictionaries)

install(TARGETS voice_server RUNTIME DESTINATION bin)
install(
    FILES
        "${opencc_SOURCE_DIR}/data/config/t2s.json"
        "${VOICE_OPENCC_DATA_DIR}/TSPhrases.ocd2"
        "${VOICE_OPENCC_DATA_DIR}/TSCharacters.ocd2"
    DESTINATION bin/opencc
)
