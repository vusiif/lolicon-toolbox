find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(TestBase64Codec
    TestBase64Codec.cpp
)

target_link_libraries(TestBase64Codec PRIVATE
    Qt6::Core
    Qt6::Test
    ToolboxCore
    ToolBase64
)

add_test(NAME TestBase64Codec COMMAND TestBase64Codec)