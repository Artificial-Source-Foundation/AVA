# CURSES_NEED_WIDE was added to 3.10
cmake_minimum_required(VERSION 3.10...4.3.2)

# Compile all subdirectories with the same standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

project(ava-ncurses-tests
        VERSION 0.0
        DESCRIPTION "Tests to see if the fundamentals required for a TUI work on all platforms."
        LANGUAGES CXX C
)

set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_executable(linux00 linux00-initialization.cpp)
target_include_directories(linux00 PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(linux00 PRIVATE ${CURSES_LIBRARIES})

add_executable(linux01 linux01-screen-size.cpp)
target_include_directories(linux01 PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(linux01 PRIVATE ${CURSES_LIBRARIES})

add_executable(linux02 linux02-screen-resizing.cpp)
target_include_directories(linux02 PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(linux02 PRIVATE ${CURSES_LIBRARIES})
