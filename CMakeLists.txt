cmake_minimum_required(VERSION 3.21)
set(TARGET_NAME "APP")
project(${TARGET_NAME} VERSION 0.1 LANGUAGES CXX)

include(qt.cmake)

add_compile_options(/utf-8)

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

file(GLOB_RECURSE LVE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lve/*.cpp" 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lve/*.h")
file(GLOB_RECURSE MOTION_CONTROL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/motion_control/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/motion_control/*.h")

set(PROJECT_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/FirstApp.h
    src/FirstApp.cpp

    ${LVE_SOURCES}
    ${MOTION_CONTROL_SOURCES}
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

target_link_libraries(${TARGET_NAME} PRIVATE "${DEPENDENCIES}/vulkan/lib/vulkan-1.lib")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${DEPENDENCIES}/vulkan/include
    ${VENDOR}/glm
    ${VENDOR}/tiny_object_loader
    ${VENDOR}/stb_image
    ${LVE}
)

#将模型资源复制到输出目录
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/res"
        "$<TARGET_FILE_DIR:${TARGET_NAME}>/res/models"
)