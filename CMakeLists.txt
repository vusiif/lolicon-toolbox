cmake_minimum_required(VERSION 3.24)

project(Toolbox
    VERSION 0.2.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(MSVC)
    # English resources are optional. Use UTF-8 output and detect the real prefix
    # rather than a cached prefix decoded with Windows' legacy system codepage.
    set(msvc_launcher "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Invoke-Msvc.cmd")
    execute_process(COMMAND cmd /d /c "${msvc_launcher}" "${CMAKE_CXX_COMPILER}"
        /nologo /showIncludes /TP /utf-8 /c "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IncludeProbe.cpp"
        "/Fo${CMAKE_BINARY_DIR}/CMakeFiles/toolbox-include-probe.obj"
        OUTPUT_VARIABLE include_output ERROR_VARIABLE include_error
        RESULT_VARIABLE include_status ENCODING UTF-8)
    set(include_output "${include_output}\n${include_error}")
    if(include_status EQUAL 0 AND include_output MATCHES "(^|\n)([^\r\n]*: +)[A-Za-z]:[/\\\\]")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    else()
        message(FATAL_ERROR "Unable to detect MSVC include output: ${include_error}")
    endif()
    set(CMAKE_CXX_COMPILER_LAUNCHER "cmd;/d;/v:off;/c;${msvc_launcher}")
endif()

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
