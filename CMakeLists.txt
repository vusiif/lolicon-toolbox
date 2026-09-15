cmake_minimum_required(VERSION 3.24)

project(Toolbox
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(tools)

option(TOOLBOX_BUILD_TESTS "Build tests" ON)
if(TOOLBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
