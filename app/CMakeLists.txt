add_executable(Toolbox WIN32
    main.cpp
    Application.h
    Application.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(Toolbox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ToolboxCore
    ToolboxUI
    ToolBase64
    dwmapi
)

target_include_directories(Toolbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
