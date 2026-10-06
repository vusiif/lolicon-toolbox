find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(TestBase64Codec TestBase64Codec.cpp)
target_link_libraries(TestBase64Codec PRIVATE Qt6::Test ToolBase64)
add_test(NAME TestBase64Codec COMMAND TestBase64Codec)
add_executable(TestUi TestUi.cpp)
target_link_libraries(TestUi PRIVATE Qt6::Test ToolboxShell ToolBase64 ToolMojibake)
add_test(NAME TestUi COMMAND TestUi -o ui-results.txt,txt)
add_executable(TestMojibake TestMojibake.cpp)
target_link_libraries(TestMojibake PRIVATE Qt6::Test MojibakeLogic)
target_compile_definitions(TestMojibake PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME TestMojibake COMMAND TestMojibake -o mojibake-results.txt,txt)
set_tests_properties(TestBase64Codec TestUi TestMojibake PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/app"
)
set_tests_properties(TestMojibake PROPERTIES TIMEOUT 90)
