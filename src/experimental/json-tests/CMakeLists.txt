cmake_minimum_required(VERSION 3.10...4.3.2)

project(ava-json-tests
        VERSION 0.0
        DESCRIPTION "Tests to see how nlohmann json works."
        LANGUAGES CXX
)

add_executable(json01 json01-read-write-stream.cpp)
target_include_directories(json01 PRIVATE ${CMAKE_SOURCE_DIR}/src/json/include)
target_link_libraries(json01 PRIVATE ${AICXX_OBJECTS_LIST})

add_executable(json02 json02-string-literal-and-initializer-list.cpp)
target_include_directories(json02 PRIVATE ${CMAKE_SOURCE_DIR}/src/json/include)
target_link_libraries(json02 PRIVATE ${AICXX_OBJECTS_LIST})

add_executable(json03 json03-intrusive-conversions.cpp)
target_include_directories(json03 PRIVATE ${CMAKE_SOURCE_DIR}/src/json/include)
target_link_libraries(json03 PRIVATE ${AICXX_OBJECTS_LIST})

add_executable(json04 json04-base-class.cpp)
target_include_directories(json04 PRIVATE ${CMAKE_SOURCE_DIR}/src/json/include)
target_link_libraries(json04 PRIVATE ${AICXX_OBJECTS_LIST})
