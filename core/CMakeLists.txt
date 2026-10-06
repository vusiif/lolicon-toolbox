add_library(ToolboxCore STATIC
    navigation/NavigationGroup.h navigation/NavigationItem.h
    navigation/NavigationManager.h navigation/NavigationManager.cpp
    tool/ToolPage.h tool/ToolPage.cpp
    tool/ToolDescriptor.h tool/ToolRegistry.h tool/ToolRegistry.cpp
    services/AppSettings.h services/AppSettings.cpp
    ai/AiProvider.h ai/AiService.h ai/AiService.cpp
    ai/providers/OpenAICompatibleProvider.h ai/providers/OpenAICompatibleProvider.cpp
)
target_include_directories(ToolboxCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(ToolboxCore PUBLIC Qt6::Core Qt6::Widgets Qt6::Network Ela::WidgetTools)
if(WIN32)
    target_link_libraries(ToolboxCore PRIVATE Crypt32)
endif()
