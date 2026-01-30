cmake_minimum_required(VERSION 3.15)
project(liblite3client CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find nlohmann_json - assuming it's available in the parent workspace or installed
find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
    # Use local external version
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external")
endif()

# Boost Dependencies (Header-only for Asio/Beast)
set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_89_0")
if(EXISTS "${BOOST_ROOT}")
    target_include_directories(nlohmann_json INTERFACE "${BOOST_ROOT}") # Just in case json needs it? No.
    # We need to add library FIRST.
endif()

add_library(lite3client STATIC
    src/client.cpp
)

if(EXISTS "${BOOST_ROOT}")
    target_include_directories(lite3client PRIVATE "${BOOST_ROOT}")
else()
    message(WARNING "Local Boost not found at ${BOOST_ROOT}, assuming system install")
    find_package(Boost REQUIRED)
    target_link_libraries(lite3client PRIVATE Boost::boost)
endif()

target_include_directories(lite3client PUBLIC
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../lite3-cpp/include
)

target_link_libraries(lite3client PUBLIC
    ws2_32
    crypt32
    # Bcrypt needed for some Boost.Asio randomness on Windows sometimes, safer to add
    bcrypt 
)

# Testing
# Standalone simple test (no GTest required)
add_executable(client_test_simple test/client_test_simple.cpp)
target_link_libraries(client_test_simple PRIVATE lite3client)
