cmake_minimum_required(VERSION 3.21)
project(photo_render LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(photo_render photo_render.cpp
    ${SRC}/knotglwidget.h ${SRC}/knotglwidget.cpp ${SRC}/knotmath.cpp ${SRC}/knottable.cpp
    ${SRC}/photoscene.cpp ${SRC}/ropephysics.cpp ${SRC}/sneaker.cpp ${SRC}/lighting.cpp ${SRC}/theme.cpp)
target_include_directories(photo_render PRIVATE ${SRC})
target_compile_options(photo_render PRIVATE -O2 -Wall -Wextra)
qt_add_resources(photo_render "knottable" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES ${SRC}/data/knottable.dat)
qt_add_resources(photo_render "photo" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES
    ${SRC}/data/photo/aft_lounge_4k.hdr ${SRC}/data/photo/country_club_4k.hdr
    ${SRC}/data/photo/wood_table_large_diff.jpg ${SRC}/data/photo/wood_table_large_nor_gl.jpg
    ${SRC}/data/photo/wood_table_large_rough.jpg ${SRC}/data/photo/wood_floor_worn_diff.jpg
    ${SRC}/data/photo/wood_floor_worn_nor_gl.jpg ${SRC}/data/photo/wood_floor_worn_rough.jpg
    ${SRC}/data/photo/sneaker.bin ${SRC}/data/photo/sneaker.json ${SRC}/data/photo/sneaker_floor.png)
target_link_libraries(photo_render PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
