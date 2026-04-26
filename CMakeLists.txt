cmake_minimum_required(VERSION 3.21)
set(TARGET_NAME "APP")
project(${TARGET_NAME} VERSION 0.1 LANGUAGES CXX)

include(qt.cmake)

add_compile_options(/utf-8)

if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Core
        Gui
        Widgets
)
qt_standard_project_setup()

file(GLOB_RECURSE GLOBAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/global/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/global/*.h")
file(GLOB_RECURSE LVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lve/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lve/*.h")
file(GLOB_RECURSE ENTITIES_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entities/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/entities/*.h")
file(GLOB_RECURSE ALGORITHM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm/*.h")
file(GLOB_RECURSE SLICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/slice/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/slice/*.h")
file(GLOB_RECURSE OPTIMIZE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/optimize/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/optimize/*.h")

set(PROJECT_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/FirstApp.h
    src/FirstApp.cpp
    src/Simulation2DDialog.h
    src/Simulation2DDialog.cpp
    src/Global.h

    ${LVE_SOURCES}
    ${ENTITIES_SOURCES}
    ${ALGORITHM_SOURCES}
    ${SLICE_SOURCES}
    ${OPTIMIZE_SOURCES}
    ${GLOBAL_SOURCES}
)

qt_add_executable(${TARGET_NAME} ${PROJECT_SOURCES})

if (WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES
        WIN32_EXECUTABLE OFF
    )
endif()

target_link_libraries(${TARGET_NAME}
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
)

set(DEPENDENCIES "${CMAKE_SOURCE_DIR}/dependencies/")
set(VENDOR "${CMAKE_SOURCE_DIR}/src/vendor")
set(LVE "${CMAKE_SOURCE_DIR}/src/lve")
set(ALGO "${CMAKE_SOURCE_DIR}/src/algorithm")
set(GLOB "${CMAKE_SOURCE_DIR}/src/global")

target_link_libraries(${TARGET_NAME} PRIVATE "${DEPENDENCIES}/vulkan/lib/vulkan-1.lib")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ALGO}
    ${DEPENDENCIES}/vulkan/include
    ${VENDOR}/glm
    ${VENDOR}/tiny_object_loader
    ${VENDOR}/stb_image
    ${VENDOR}/entt
    ${VENDOR}/tinynurbs
    ${VENDOR}/renderdoc
    ${VENDOR}/tkspline
    ${LVE}
    ${GLOB}
)

#将模型资源复制到输出目录
#[[add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/res"
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/res/models"
)]]

add_custom_target(CompileShaders
    # 使用 start /WAIT 命令打开一个新标题为 "Shader Compiler" 的独立 cmd 窗口
    COMMAND start "Shader Compiler" /WAIT cmd.exe /c "${CMAKE_SOURCE_DIR}/compile_shader.bat"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Auto-compiling Vulkan shaders in a new window..."
)

add_dependencies(${TARGET_NAME} CompileShaders)