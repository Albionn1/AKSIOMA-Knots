cmake_minimum_required(VERSION 3.21)

project(AKSIOMAKnots VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt 6 with the OpenGL widget module. GuiPrivate is only used for one Windows
# setting in main.cpp (pop-ups in full screen), so its warning is turned off.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS Gui GuiPrivate Widgets OpenGL OpenGLWidgets Svg LinguistTools)

qt_standard_project_setup()

qt_add_executable(AKSIOMAKnots
    main.cpp
    mainwindow.h
    mainwindow.cpp
    knotglwidget.h
    knotglwidget.cpp
    knotmath.h
    knotmath.cpp
    knotrelax.h
    knotrelax.cpp
    knotdiagram.h
    knotdiagram.cpp
    knotreidemeister.h
    knotreidemeister.cpp
    diagrameditor.h
    diagrameditor.cpp
    knotpractice.h
    knotpractice.cpp
    practicedialog.h
    practicedialog.cpp
    knotexport.h
    knotexport.cpp
    knotseifert.h
    knotseifert.cpp
    knotjoin.h
    knotjoin.cpp
    photoscene.h
    photoscene.cpp
    timelineslider.h
    timelineslider.cpp
    knottable.h
    knottable.cpp
    knotbrowser.h
    knotbrowser.cpp
    aboutdialog.h
    aboutdialog.cpp
    unknotsummarydialog.h
    unknotsummarydialog.cpp
    appsettings.h
    appsettings.cpp
    lighting.h
    lighting.cpp
    gpumemory.h
    gpumemory.cpp
    theme.h
    theme.cpp
    resources.qrc
)

# Icon of the .exe in Windows Explorer (the window icon is set in main.cpp).
if(WIN32)
    # a window program: no console window next to it
    set_target_properties(AKSIOMAKnots PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(AKSIOMAKnots PRIVATE app.rc)
    # SHChangeNotify: Explorer shows the icon of .aknot files (appsettings.cpp)
    target_link_libraries(AKSIOMAKnots PRIVATE shell32)
    # video memory of the program (gpumemory.cpp)
    target_link_libraries(AKSIOMAKnots PRIVATE dxgi)
endif()

# Knot table (KnotInfo, built by tools/make_knot_table.cpp). About 9 MB, so it
# is linked in as binary data instead of being turned into C++ source.
qt_add_resources(AKSIOMAKnots "knottable"
    PREFIX "/"
    BIG_RESOURCES
    FILES data/knottable.dat
)

# Photo mode: two rooms (HDRIs) and two surfaces from Poly Haven, CC0
# (data/photo/CREDITS.txt). About 15 MB, linked in as binary data too.
qt_add_resources(AKSIOMAKnots "photo"
    PREFIX "/"
    BIG_RESOURCES
    FILES
        data/photo/brown_photostudio_02_2k.hdr
        data/photo/lebombo_2k.hdr
        data/photo/wood_table_001_diff.jpg
        data/photo/wood_table_001_nor_gl.jpg
        data/photo/wood_table_001_rough.jpg
        data/photo/laminate_floor_02_diff.jpg
        data/photo/laminate_floor_02_nor_gl.jpg
        data/photo/laminate_floor_02_rough.jpg
)

target_link_libraries(AKSIOMAKnots PRIVATE
    Qt6::Widgets
    # QWindowsApplication (full screen pop-up fix in main.cpp)
    Qt6::GuiPrivate
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    # SVG export of knot diagrams
    Qt6::Svg
)

if(MSVC)
    target_compile_options(AKSIOMAKnots PRIVATE /W3 /utf-8)
else()
    target_compile_options(AKSIOMAKnots PRIVATE -Wall -Wextra)

    # The relaxation runs tens of thousands of steps while you watch, so it
    # is optimised in Debug builds too (about ten times faster).
    set_source_files_properties(knotrelax.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()

# Albanian translation, compiled into the program as :/i18n/KnotTheory_sq_AL.qm
# (chosen in the Settings tab).
qt_add_translations(AKSIOMAKnots
    TS_FILES KnotTheory_sq_AL.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -tr-function-alias tr+=knotTr,tr+=exportTr,tr+=seifertTr,tr+=joinTr
)
