cmake_minimum_required(VERSION 3.20)
project(tempdrop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.1
)
FetchContent_MakeAvailable(crow)

find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(tempdrop
    src/main.cpp
    src/config.cpp
    src/database.cpp
    src/storage.cpp
    src/cleanup.cpp
    src/api.cpp
)

target_include_directories(tempdrop PRIVATE src)

target_link_libraries(tempdrop PRIVATE
    Crow::Crow
    SQLite::SQLite3
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_options(tempdrop PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)

install(TARGETS tempdrop RUNTIME DESTINATION bin)
