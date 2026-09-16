add_library(ToolboxCore STATIC
    tool/ToolPage.h
    tool/ToolPage.cpp
    tool/ToolDescriptor.h
    tool/ToolRegistry.h
    tool/ToolRegistry.cpp
    tool/ToolPageManager.h
    tool/ToolPageManager.cpp
    navigation/NavigationGroup.h
    navigation/NavigationItem.h
    navigation/NavigationManager.h
    navigation/NavigationManager.cpp
    services/SettingsService.h
    services/SettingsService.cpp
    services/AppServices.h
    services/AppServices.cpp
    theme/Theme.h
    theme/Theme.cpp
)

target_include_directories(ToolboxCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ToolboxCore PUBLIC
    Qt6::Core
    Qt6::Widgets
)
