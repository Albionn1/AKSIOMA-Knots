cmake_minimum_required(VERSION 3.21)
project(film_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(film_test film_test.cpp
    ${SRC}/knotmath.cpp ${SRC}/knottable.cpp ${SRC}/knotdiagram.cpp ${SRC}/knotpractice.cpp
    ${SRC}/knotseifert.cpp ${SRC}/knotfilm.cpp)
target_include_directories(film_test PRIVATE ${SRC})
target_compile_options(film_test PRIVATE -O2 -Wall -Wextra)
qt_add_resources(film_test "knottable" PREFIX "/" BIG_RESOURCES BASE ${SRC} FILES ${SRC}/data/knottable.dat)
target_link_libraries(film_test PRIVATE Qt6::Core Qt6::Gui)
