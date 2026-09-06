# CURSES_NEED_WIDE was added to 3.10
cmake_minimum_required(VERSION 3.10...4.3.2)

project(ava-allocator-tests
        VERSION 0.0
        DESCRIPTION "Tests to experiment with allocators."
        LANGUAGES CXX C
)

add_executable(allocator01 allocator01-simple-vector-allocator.cpp)
target_link_libraries(allocator01 PRIVATE ${AICXX_OBJECTS_LIST})
target_include_directories(allocator01 PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_executable(elements_to_allocation_size_test elements_to_allocation_size_test.cpp)
target_link_libraries(elements_to_allocation_size_test PRIVATE ${AICXX_OBJECTS_LIST})
target_include_directories(elements_to_allocation_size_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
