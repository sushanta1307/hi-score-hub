cmake_minimum_required(VERSION 3.24)
project(gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------- dependencies -----------------------------------------------
find_package(Protobuf REQUIRED)           # provides protobuf::libprotobuf
find_package(PkgConfig REQUIRED)

#   cppkafka and rdkafka (via pkg-config)
pkg_check_modules(CPPKAFKA REQUIRED IMPORTED_TARGET cppkafka)
pkg_check_modules(RDKAFKA  REQUIRED IMPORTED_TARGET rdkafka)

# -------- sources -----------------------------------------------------
file(GLOB SRC
     src/*.cpp
     src/generated/*.cc)          # <-- pulls in score_update.pb.cc

add_executable(gateway ${SRC})

target_include_directories(gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/proto
    ${Protobuf_INCLUDE_DIRS})

target_link_libraries(gateway
    protobuf::libprotobuf
    PkgConfig::CPPKAFKA       # pulls in cppkafka + rdkafka automatically
)