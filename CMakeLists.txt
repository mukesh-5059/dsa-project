cmake_minimum_required(VERSION 3.15)
project(DSA_Project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Standard libraries
find_package(ZLIB REQUIRED)
find_package(EXPAT REQUIRED)
find_package(BZip2 REQUIRED)

# 2. Boost & Raylib
find_package(Boost REQUIRED COMPONENTS program_options)
find_package(raylib REQUIRED)

# 3. Osmium & Protozero
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Osmium REQUIRED COMPONENTS io)

add_executable(${PROJECT_NAME} 
    src/main.cpp 
    src/map_reader.cpp 
    src/map_renderer.cpp
)

# 4. Link everything
target_include_directories(${PROJECT_NAME} PRIVATE src ${OSMIUM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${OSMIUM_LIBRARIES}
    Boost::program_options
    raylib
)