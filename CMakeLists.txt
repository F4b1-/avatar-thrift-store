# PC build: core library, avtool CLI and unit tests.
# The Xbox 360 app is built separately with build_xbox.ps1 (XDK toolchain).
cmake_minimum_required(VERSION 3.20)
project(avatar_thrift_store CXX)

set(CMAKE_CXX_STANDARD 11)

if(MSVC)
    add_compile_options(/W4 /D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(core STATIC
    core/account.cpp
    core/avatar_item.cpp
    core/gpd.cpp
    core/profile.cpp
    core/rsa.cpp
    core/stfs_package.cpp
    core/sha1.cpp
    core/stfs.cpp
)
target_include_directories(core PUBLIC core)

add_executable(avtool tools/avtool/main.cpp)
target_link_libraries(avtool core)

enable_testing()
add_executable(core_tests tests/test_main.cpp)
target_link_libraries(core_tests core)
add_test(NAME core_tests COMMAND core_tests)
