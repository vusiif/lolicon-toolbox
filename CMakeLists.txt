cmake_minimum_required(VERSION 3.24)

project(Toolbox
    VERSION 0.2.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WidgetsPrivate LinguistTools)

# Build the vendored Ela controls without the example application or a runtime DLL.
set(QT_VERSION_MAJOR 6)
set(ELAWIDGETTOOLS_BUILD_STATIC_LIB ON CACHE BOOL "Build Ela statically" FORCE)
add_subdirectory(third_party/ElaWidgetTools-main/ElaWidgetTools EXCLUDE_FROM_ALL)

set(TS_FILES
    translations/zh_CN.ts
    translations/zh_TW.ts
)


add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(tools)

option(TOOLBOX_BUILD_TESTS "Build tests" ON)
if(TOOLBOX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(translation_targets Toolbox)
if(TARGET TestUi)
    list(APPEND translation_targets TestUi)
endif()
qt_add_translations(TARGETS ${translation_targets} TS_FILES ${TS_FILES})
