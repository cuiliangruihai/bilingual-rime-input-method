cmake_minimum_required(VERSION 3.27)

project(url-filter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

option(ENABLE_TEST "Build Test" On)

add_custom_command(
    OUTPUT ${PROJECT_SOURCE_DIR}/src/rules.cpp
    COMMAND ${PROJECT_SOURCE_DIR}/transform_json.py
    DEPENDS ${PROJECT_SOURCE_DIR}/Rules/data.min.json
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generating ClearURLs Rules header..."
)
add_custom_target(TransformJSON ALL
    DEPENDS ${PROJECT_SOURCE_DIR}/src/rules.cpp
)

add_library(url-filter STATIC
    src/url-filter.cpp
    src/rules.cpp
    src/util.cpp
)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif()
