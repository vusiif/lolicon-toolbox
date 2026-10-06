add_library(ToolboxCore STATIC
    navigation/NavigationGroup.h navigation/NavigationItem.h
    navigation/NavigationManager.h navigation/NavigationManager.cpp
    tool/ToolPage.h tool/ToolPage.cpp
    tool/ToolDescriptor.h tool/ToolRegistry.h tool/ToolRegistry.cpp
    services/AppSettings.h services/AppSettings.cpp
)
target_include_directories(ToolboxCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ToolboxCore PUBLIC Qt6::Core Qt6::Widgets Ela::WidgetTools)
