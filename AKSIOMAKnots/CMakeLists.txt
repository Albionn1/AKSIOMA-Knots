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
find_package(Qt6 REQUIRED COMPONENTS Gui GuiPrivate Widgets OpenGL OpenGLWidgets Svg Multimedia LinguistTools)

qt_standard_project_setup()

# AKSIOMA Engine: the 3D renderer, a project of its own (cmake/AksiomaEngine.cmake)
include(cmake/AksiomaEngine.cmake)

qt_add_executable(AKSIOMAKnots
    main.cpp
    mainwindow.h
    mainwindow.cpp
    knotglwidget.h
    knotglwidget.cpp
    trainlayer.h
    trainlayer.cpp
    knotoverlay.h
    knotoverlay.cpp
    knotmath.h
    knotmath.cpp
    knothistory.h
    knothistory.cpp
    commandpalette.h
    commandpalette.cpp
    controlhelp.h
    controlhelp.cpp
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
    knotfilm.cpp
    knotjoin.h
    knotjoin.cpp
    photoscene.h
    photoscene.cpp
    ropephysics.h
    ropephysics.cpp
    traintrack.h
    traintrack.cpp
    trainsynth.h
    trainsynth.cpp
    trainsound.h
    trainsound.cpp
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
    gpumemory.h
    icons.h
    icons.cpp
    theme.h
    theme.cpp
    resources.qrc
)

# Icon of the .exe in Windows Explorer (the window icon is set in main.cpp).
if(WIN32)
    # a window program: no console window next to it
    set_target_properties(AKSIOMAKnots PROPERTIES WIN32_EXECUTABLE TRUE)
    # (on its own: MinGW's resource compiler cannot take the include path of
    # AKSIOMA Engine when it has a space in it, as "Documents/AKSIOMA Engine")
    add_library(AKSIOMAKnots_icon OBJECT app.rc)
    target_sources(AKSIOMAKnots PRIVATE $<TARGET_OBJECTS:AKSIOMAKnots_icon>)
    # SHChangeNotify: Explorer shows the icon of .aknot files (appsettings.cpp)
    target_link_libraries(AKSIOMAKnots PRIVATE shell32)
endif()

# Knot table (KnotInfo, built by tools/make_knot_table.cpp). About 9 MB, so it
# is linked in as binary data instead of being turned into C++ source.
qt_add_resources(AKSIOMAKnots "knottable"
    PREFIX "/"
    BIG_RESOURCES
    FILES data/knottable.dat
)

# Photo mode: three places (unclipped 4k HDRIs) and two wood surfaces
# from Poly Haven, CC0 (data/photo/CREDITS.txt). About 100 MB, linked in as
# binary data too.
qt_add_resources(AKSIOMAKnots "photo"
    PREFIX "/"
    BIG_RESOURCES
    FILES
        data/photo/chinese_garden_4k.hdr
        data/photo/meadow_2_4k.hdr
        data/photo/evening_museum_courtyard_4k.hdr
        data/photo/wood_table_large_diff.jpg
        data/photo/wood_table_large_nor_gl.jpg
        data/photo/wood_table_large_rough.jpg
        data/photo/wood_floor_worn_diff.jpg
        data/photo/wood_floor_worn_nor_gl.jpg
        data/photo/wood_floor_worn_rough.jpg
)

# This app's own shaders (the toy train, its steam and its wooden track), beside
# the engine's:
# :/knots/shaders/... (AKSIOMA_KNOTS_SHADER_DIR=<this folder>/shaders reads
# them from disk instead, with live reload).
qt_add_resources(AKSIOMAKnots "knots_shaders"
    PREFIX "/knots"
    FILES
        shaders/train.vert
        shaders/train.frag
        shaders/steam.vert
        shaders/steam.frag
        shaders/track_material.glsl
)

# One place for the version: project() above, for the program and the installer.
target_compile_definitions(AKSIOMAKnots PRIVATE AKSIOMA_VERSION="${PROJECT_VERSION}")

target_link_libraries(AKSIOMAKnots PRIVATE
    # the 3D renderer (AKSIOMA Engine)
    Aksioma::Engine
    Qt6::Widgets
    # QWindowsApplication (full screen pop-up fix in main.cpp)
    Qt6::GuiPrivate
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    # SVG export of knot diagrams
    Qt6::Svg
    # the toy train's sound
    Qt6::Multimedia
)

if(MSVC)
    target_compile_options(AKSIOMAKnots PRIVATE /W3 /utf-8)
else()
    target_compile_options(AKSIOMAKnots PRIVATE -Wall -Wextra)

    # The relaxation runs tens of thousands of steps while you watch, and
    # the rope in Photo mode is simulated and rebuilt for every frame while
    # it is in the hand, so these are optimised in Debug builds too (about
    # ten times faster). The same for spreading a drawing apart and for
    # relaxing a Seifert surface into its film.
    set_source_files_properties(knotrelax.cpp ropephysics.cpp knotmath.cpp knotglwidget.cpp knotdiagram.cpp knotfilm.cpp traintrack.cpp trainsynth.cpp PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()

# Albanian translation, compiled into the program as :/i18n/KnotTheory_sq_AL.qm
# (chosen in the Settings tab).
qt_add_translations(AKSIOMAKnots
    TS_FILES KnotTheory_sq_AL.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -tr-function-alias tr+=knotTr,tr+=exportTr,tr+=seifertTr,tr+=joinTr
)
