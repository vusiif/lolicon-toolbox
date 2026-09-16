add_library(ToolBase64 STATIC
    Base64Codec.h
    Base64Codec.cpp
    Base64Page.h
    Base64Page.cpp
)

target_include_directories(ToolBase64 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(ToolBase64 PUBLIC
    Qt6::Core
    Qt6::Widgets
    ToolboxCore
)
