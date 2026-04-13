cmake_minimum_required(VERSION 3.20)
project(Pico)

# Исправляем проблему со старыми зависимостями
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# httplib
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.14.3
)

# zstd
FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.5
        SOURCE_SUBDIR build/cmake
)

set(ZSTD_BUILD_PROGRAMS OFF CACHE INTERNAL "")
set(ZSTD_BUILD_TESTS OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(httplib zstd)

# Создаем бинарник
add_executable(Pico main.cpp)

# Указываем, где лежат наши хидеры
target_include_directories(Pico PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${httplib_SOURCE_DIR}
        ${zstd_SOURCE_DIR}/lib
)

target_link_libraries(Pico PRIVATE
        httplib::httplib
        libzstd_static
)

if(WIN32)
    target_link_libraries(Pico PRIVATE ws2_32)
endif()