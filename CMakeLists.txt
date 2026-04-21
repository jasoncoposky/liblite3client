cmake_minimum_required(VERSION 3.15)
project(liblite3client CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_89_0")
find_package(Threads REQUIRED)

# ZeroMQ Integration
include(FetchContent)
FetchContent_Declare(
  libzmq
  GIT_REPOSITORY https://github.com/zeromq/libzmq.git
  GIT_TAG v4.3.5
)
FetchContent_Declare(
  cppzmq
  GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
  GIT_TAG v4.10.0
)

# Set ZeroMQ build options
set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libzmq cppzmq)

add_library(lite3client STATIC
    src/client.cpp
    src/smart_client.cpp
    src/async_client.cpp
    src/write_coordinator.cpp
)

if(MSVC)
    add_definitions(-D_WIN32_WINNT=0x0A00 -DNOMINMAX)
    target_compile_options(lite3client PRIVATE /bigobj)
endif()

if(EXISTS "${BOOST_ROOT}")
    target_include_directories(lite3client PUBLIC "${BOOST_ROOT}")
else()
    # Boost is becoming optional for transport but might be used for other utilities
    find_package(Boost REQUIRED)
    target_include_directories(lite3client PUBLIC ${Boost_INCLUDE_DIRS})
endif()


target_include_directories(lite3client PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lite3-cpp/include
)

target_link_libraries(lite3client PUBLIC
    Threads::Threads
    libzmq
    cppzmq
    ws2_32
    crypt32
    # Bcrypt needed for some Boost.Asio randomness on Windows sometimes, safer to add
    bcrypt
)


# Testing
# Standalone simple test (no GTest required)
add_executable(client_test_simple test/client_test_simple.cpp)
target_link_libraries(client_test_simple PRIVATE lite3client lite3-cpp)

add_executable(test_async_debug test/test_async_debug.cpp)
target_link_libraries(test_async_debug PRIVATE lite3client lite3-cpp)
