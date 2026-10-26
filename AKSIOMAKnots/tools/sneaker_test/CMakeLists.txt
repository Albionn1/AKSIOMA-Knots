cmake_minimum_required(VERSION 3.21)
project(sneaker_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(sneaker_test sneaker_test.cpp
    ${SRC}/knotmath.cpp ${SRC}/knottable.cpp ${SRC}/photoscene.cpp ${SRC}/ropephysics.cpp
    ${SRC}/sneaker.cpp)
target_include_directories(sneaker_test PRIVATE ${SRC})
target_compile_options(sneaker_test PRIVATE -O2 -Wall -Wextra)
qt_add_resources(sneaker_test "data" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES
    ${SRC}/data/knottable.dat
    ${SRC}/data/photo/sneaker.bin
    ${SRC}/data/photo/sneaker.json)
target_link_libraries(sneaker_test PRIVATE Qt6::Core Qt6::Gui)
