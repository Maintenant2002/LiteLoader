# test/CMakeLists.txt - LiteLoader Windows 测试构建
#
# 用法：
#   cd test
#   cmake -B build -G "MinGW Makefiles"
#   cmake --build build
#   python gen_firmware.py -o build\test_firmware.bin
#   build\lite_loader_test.exe build\test_firmware.bin build\flash_out.bin

cmake_minimum_required(VERSION 3.15)

# ---- MinGW 工具链（放在 project() 之前） ----
if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER   "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/gcc.exe")
    set(CMAKE_CXX_COMPILER "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/g++.exe")
    set(CMAKE_RC_COMPILER  "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/windres.exe")
endif()

project(lite_loader_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- 引入 LiteLoader 核心库 ----
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. lite_loader_build)

# ---- 测试可执行文件 ----
set(TEST_SOURCES
    main_test.c
    port_win32.c
)

add_executable(lite_loader_test ${TEST_SOURCES})
target_link_libraries(lite_loader_test PRIVATE lite_loader)

# Windows 子系统设为控制台
set_target_properties(lite_loader_test PROPERTIES
    WIN32_EXECUTABLE OFF
)

# ---- gen_firmware 目标 ----
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
                -o ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
        COMMENT "Generating test firmware binary..."
    )
    add_custom_target(gen_firmware ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
    )
    add_dependencies(lite_loader_test gen_firmware)
else()
    message(WARNING "Python3 not found, skip gen_firmware target. "
                    "Run manually: python gen_firmware.py -o test_firmware.bin")
endif()

# ---- 测试运行 ----
enable_testing()

add_test(
    NAME    boot_process_test
    COMMAND lite_loader_test
            ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
            ${CMAKE_CURRENT_BINARY_DIR}/flash_out.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
