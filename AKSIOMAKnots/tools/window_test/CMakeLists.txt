cmake_minimum_required(VERSION 3.21)
project(window_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Svg Multimedia LinguistTools)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(window_test window_test.cpp
    ${SRC}/mainwindow.h ${SRC}/mainwindow.cpp
    ${SRC}/knotglwidget.h ${SRC}/knotglwidget.cpp
    ${SRC}/knotmath.h ${SRC}/knotmath.cpp
    ${SRC}/knothistory.h ${SRC}/knothistory.cpp
    ${SRC}/commandpalette.h ${SRC}/commandpalette.cpp
    ${SRC}/controlhelp.h ${SRC}/controlhelp.cpp
    ${SRC}/knotrelax.h ${SRC}/knotrelax.cpp
    ${SRC}/knotdiagram.h ${SRC}/knotdiagram.cpp
    ${SRC}/knotreidemeister.h ${SRC}/knotreidemeister.cpp
    ${SRC}/diagrameditor.h ${SRC}/diagrameditor.cpp
    ${SRC}/knotpractice.h ${SRC}/knotpractice.cpp
    ${SRC}/practicedialog.h ${SRC}/practicedialog.cpp
    ${SRC}/knotexport.h ${SRC}/knotexport.cpp
    ${SRC}/knotseifert.h ${SRC}/knotseifert.cpp ${SRC}/knotfilm.cpp
    ${SRC}/knotjoin.h ${SRC}/knotjoin.cpp
    ${SRC}/photoscene.h ${SRC}/photoscene.cpp
    ${SRC}/ropephysics.h ${SRC}/ropephysics.cpp
    ${SRC}/traintrack.h ${SRC}/traintrack.cpp
    ${SRC}/trainsynth.h ${SRC}/trainsynth.cpp
    ${SRC}/trainsound.h ${SRC}/trainsound.cpp
    ${SRC}/timelineslider.h ${SRC}/timelineslider.cpp
    ${SRC}/knottable.h ${SRC}/knottable.cpp
    ${SRC}/knotbrowser.h ${SRC}/knotbrowser.cpp
    ${SRC}/aboutdialog.h ${SRC}/aboutdialog.cpp
    ${SRC}/unknotsummarydialog.h ${SRC}/unknotsummarydialog.cpp
    ${SRC}/appsettings.h ${SRC}/appsettings.cpp
    ${SRC}/lighting.h ${SRC}/lighting.cpp
    ${SRC}/gpumemory.h ${SRC}/gpumemory.cpp
    ${SRC}/icons.h ${SRC}/icons.cpp
    ${SRC}/theme.h ${SRC}/theme.cpp
    ${SRC}/resources.qrc)
target_include_directories(window_test PRIVATE ${SRC})
target_compile_options(window_test PRIVATE -O2 -Wall -Wextra)
qt_add_resources(window_test "knottable" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES ${SRC}/data/knottable.dat)
qt_add_resources(window_test "photo" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES
    ${SRC}/data/photo/chinese_garden_4k.hdr ${SRC}/data/photo/meadow_2_4k.hdr ${SRC}/data/photo/evening_museum_courtyard_4k.hdr
    ${SRC}/data/photo/wood_table_large_diff.jpg ${SRC}/data/photo/wood_table_large_nor_gl.jpg
    ${SRC}/data/photo/wood_table_large_rough.jpg ${SRC}/data/photo/wood_floor_worn_diff.jpg
    ${SRC}/data/photo/wood_floor_worn_nor_gl.jpg ${SRC}/data/photo/wood_floor_worn_rough.jpg)
qt_add_translations(window_test TS_FILES ${SRC}/KnotTheory_sq_AL.ts RESOURCE_PREFIX /i18n)
target_link_libraries(window_test PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Svg Qt6::Multimedia shell32 dxgi)
