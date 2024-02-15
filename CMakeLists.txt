cmake_minimum_required(VERSION 3.27)
project(async_rpc)

set(CMAKE_CXX_STANDARD 20)

if (NOT SANITIZER)
    set(SANITIZER address)
endif ()
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address")


find_package(Boost REQUIRED)

set(SRC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/src/include)
set(TEST_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/test/include)

include_directories(
        ${SRC_INCLUDE_DIR}
        ${TEST_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/third-party/json/include
)

file(GLOB_RECURSE
        SRCS                    # Store the list of files into the variable ${SRCS}.
        CONFIGURE_DEPENDS       # Ask CMake to regenerate the build system if these files change.
        ${PROJECT_SOURCE_DIR}/src/*.cpp
)

set(THIRDPARTY_LIBS
        boost_fiber
        boost_context
)

enable_testing()
add_subdirectory(test)
add_subdirectory(third-party)

add_library(async_rpc STATIC ${SRCS}
        ${SRCS}
)

target_link_libraries(async_rpc ${THIRDPARTY_LIBS})

add_executable(main main.cpp)
target_link_libraries(main async_rpc)

