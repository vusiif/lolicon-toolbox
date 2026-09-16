add_library(ToolboxUI STATIC
    Sidebar.h
    Sidebar.cpp
)

target_include_directories(ToolboxUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ToolboxUI PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ToolboxCore
)