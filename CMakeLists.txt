cmake_minimum_required(VERSION 3.20)
project(MMOEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SERVER "Compilar servidor" ON)
option(BUILD_TESTS  "Compilar testes"   ON)

include(FetchContent)

FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)

# Asio standalone (sem Boost)
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2)

FetchContent_MakeAvailable(entt spdlog json asio)

# Target de interface para Asio (header-only)
add_library(asio_interface INTERFACE)
target_include_directories(asio_interface INTERFACE
    ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_interface INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
    _WIN32_WINNT=0x0601)
if(WIN32)
    target_link_libraries(asio_interface INTERFACE ws2_32 mswsock)
endif()

set(PYBIND11_FINDPYTHON ON)
set(Python3_FIND_STRATEGY VERSION)
find_package(Python3 3.12...<3.13 REQUIRED COMPONENTS Interpreter Development)

message(STATUS "Python encontrado: ${Python3_EXECUTABLE}")
message(STATUS "Python versao: ${Python3_VERSION}")
message(STATUS "Python libs: ${Python3_LIBRARIES}")

FetchContent_Declare(pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6)
FetchContent_MakeAvailable(pybind11)

add_subdirectory(engine)
add_subdirectory(server)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
