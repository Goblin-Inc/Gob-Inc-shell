cmake_minimum_required(VERSION 3.14)
project(MyGUI_Shell)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.9
)
FetchContent_MakeAvailable(imgui)

add_executable(myshell_gui
    main.cpp
    shell.cpp
    process/pty.cpp
    terminal/terminal.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(myshell_gui PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    .    # <--- ВОТ ЭТА ТОЧКА УКАЗЫВАЕТ КОМПИЛЯТОРУ ИСКАТЬ ФАЙЛЫ В КОРНЕ ПРОЕКТА
)

find_package(OpenGL REQUIRED)
target_link_libraries(myshell_gui PRIVATE glfw OpenGL::GL)