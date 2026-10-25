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
find_package(Qt6 REQUIRED COMPONENTS Gui GuiPrivate Widgets OpenGL OpenGLWidgets LinguistTools)

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
    diagrameditor.h
    diagrameditor.cpp
    knotpractice.h
    knotpractice.cpp
    practicedialog.h
    practicedialog.cpp
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
    theme.h
    theme.cpp
    resources.qrc
)

# Icon of the .exe in Windows Explorer (the window icon is set in main.cpp).
if(WIN32)
    target_sources(AKSIOMAKnots PRIVATE app.rc)
endif()

# Knot table (KnotInfo, built by tools/make_knot_table.cpp). About 9 MB, so it
# is linked in as binary data instead of being turned into C++ source.
qt_add_resources(AKSIOMAKnots "knottable"
    PREFIX "/"
    BIG_RESOURCES
    FILES data/knottable.dat
)

target_link_libraries(AKSIOMAKnots PRIVATE
    Qt6::Widgets
    # QWindowsApplication (full screen pop-up fix in main.cpp)
    Qt6::GuiPrivate
    Qt6::OpenGL
    Qt6::OpenGLWidgets
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
# (chosen in the Settings tab). After changing texts in the code, build the
# "update_translations" target to add the new texts to the .ts file, then
# translate them (Qt Linguist or any text editor).
qt_add_translations(AKSIOMAKnots
    TS_FILES KnotTheory_sq_AL.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none -tr-function-alias tr+=knotTr
)
