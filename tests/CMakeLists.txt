find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(TestBase64Codec TestBase64Codec.cpp)
target_link_libraries(TestBase64Codec PRIVATE Qt6::Test ToolBase64)
add_test(NAME TestBase64Codec COMMAND TestBase64Codec)
add_executable(TestUi TestUi.cpp)
target_link_libraries(TestUi PRIVATE Qt6::Test ToolboxShell ToolBase64)
add_test(NAME TestUi COMMAND TestUi -o ui-results.txt,txt)
set_tests_properties(TestBase64Codec TestUi PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${CMAKE_BINARY_DIR}/app"
)
