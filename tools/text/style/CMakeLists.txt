find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
set(style_json "${PROJECT_SOURCE_DIR}/resources/styles/mars_classic.json")
add_custom_target(CheckStyleJson ALL
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/Check-Style-Json.py" "${style_json}" "${PROJECT_SOURCE_DIR}/tests/data/style-demo.json"
    VERBATIM)
add_library(TextStyleLogic STATIC TextStyleEngine.h TextStyleEngine.cpp StyleLoader.cpp)
target_include_directories(TextStyleLogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(TextStyleLogic PUBLIC Qt6::Core)
add_dependencies(TextStyleLogic CheckStyleJson)
qt_add_resources(TextStyleLogic styles PREFIX "/styles" BASE "${PROJECT_SOURCE_DIR}/resources/styles" FILES "${style_json}")
add_library(ToolTextStyle STATIC TextStylePage.h TextStylePage.cpp)
target_link_libraries(ToolTextStyle PUBLIC TextStyleLogic ToolboxUI Qt6::Concurrent)
target_include_directories(ToolTextStyle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
