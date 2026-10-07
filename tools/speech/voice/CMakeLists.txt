find_package(Qt6 REQUIRED COMPONENTS Network)
add_library(VoiceDesignLogic STATIC MiMoTtsService.h MiMoTtsService.cpp)
target_include_directories(VoiceDesignLogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(VoiceDesignLogic PUBLIC ToolboxCore Qt6::Network)
