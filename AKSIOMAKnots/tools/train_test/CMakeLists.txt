cmake_minimum_required(VERSION 3.21)
project(train_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(train_test train_test.cpp
    ${SRC}/knotmath.cpp ${SRC}/knottable.cpp ${SRC}/knotdiagram.cpp ${SRC}/photoscene.cpp
    ${SRC}/ropephysics.cpp ${SRC}/traintrack.cpp ${SRC}/trainsynth.cpp)
target_include_directories(train_test PRIVATE ${SRC})
target_compile_options(train_test PRIVATE -O2 -Wall -Wextra)
qt_add_resources(train_test "data" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES ${SRC}/data/knottable.dat)
target_link_libraries(train_test PRIVATE Qt6::Core Qt6::Gui)
