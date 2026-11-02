cmake_minimum_required(VERSION 3.21)
project(seifert_render LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(seifert_render seifert_render.cpp
    ${SRC}/knotglwidget.h ${SRC}/knotglwidget.cpp ${SRC}/knotmath.cpp ${SRC}/knottable.cpp
    ${SRC}/knotdiagram.cpp ${SRC}/knotpractice.cpp ${SRC}/knotseifert.cpp ${SRC}/knotfilm.cpp
    ${SRC}/photoscene.cpp ${SRC}/ropephysics.cpp ${SRC}/traintrack.cpp ${SRC}/lighting.cpp ${SRC}/theme.cpp)
target_include_directories(seifert_render PRIVATE ${SRC})
target_compile_options(seifert_render PRIVATE -O2 -Wall -Wextra)
qt_add_resources(seifert_render "knottable" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES ${SRC}/data/knottable.dat)
qt_add_resources(seifert_render "photo" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES
    ${SRC}/data/photo/chinese_garden_4k.hdr ${SRC}/data/photo/meadow_2_4k.hdr ${SRC}/data/photo/evening_museum_courtyard_4k.hdr
    ${SRC}/data/photo/wood_table_large_diff.jpg ${SRC}/data/photo/wood_table_large_nor_gl.jpg
    ${SRC}/data/photo/wood_table_large_rough.jpg ${SRC}/data/photo/wood_floor_worn_diff.jpg
    ${SRC}/data/photo/wood_floor_worn_nor_gl.jpg ${SRC}/data/photo/wood_floor_worn_rough.jpg)
target_link_libraries(seifert_render PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
