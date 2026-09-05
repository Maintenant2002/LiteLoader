# LiteLoader Windows 测试构建
#
# 用法：
#   cd test
#   cmake -B build -G "MinGW Makefiles"
#   cmake --build build
#   build\lite_loader_test.exe build\test_firmware.bin build\flash_out.bin

cmake_minimum_required(VERSION 3.15)

# ---- MinGW 工具链 ----
set(CMAKE_C_COMPILER   "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/g++.exe")
set(CMAKE_RC_COMPILER  "D:/dev-tools/tools/compilers/mingw64-14.2.0/bin/windres.exe")

project(lite_loader_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- 引入父目录的 loader 库 ----
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. lite_loader_build)

# ---- 测试可执行文件 ----
add_executable(lite_loader_test
    main_test.c
    port_win32.c
)
target_link_libraries(lite_loader_test PRIVATE lite_loader)
target_compile_definitions(lite_loader_test PRIVATE _CRT_SECURE_NO_WARNINGS)

# ---- 自动运行 gen_firmware.py 生成测试固件 ----
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
                -o ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_firmware.py
        COMMENT "Generating XMODEM-CRC test firmware"
    )
    add_custom_target(gen_firmware ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
    )
    add_dependencies(lite_loader_test gen_firmware)
endif()

# ---- CTest ----
enable_testing()
add_test(
    NAME    boot_process_test
    COMMAND lite_loader_test
            ${CMAKE_CURRENT_BINARY_DIR}/test_firmware.bin
            ${CMAKE_CURRENT_BINARY_DIR}/flash_out.bin
)
