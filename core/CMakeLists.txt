add_library(ToolboxCore STATIC
    tool/ToolPage.h
    tool/ToolDescriptor.h
    tool/ToolRegistry.h
    tool/ToolRegistry.cpp
)

target_include_directories(ToolboxCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ToolboxCore PUBLIC
    Qt6::Core
    Qt6::Widgets
)
