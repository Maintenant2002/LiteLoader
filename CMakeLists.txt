# LiteLoader - 组件 CMake（嵌入式或被 test/CMakeLists.txt 引用）
#
# 提供目标：lite_loader（静态库）

cmake_minimum_required(VERSION 3.15)

# ---- 核心源文件 ----
set(LOADER_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/states.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/proto_xmodem.c
)

set(LOADER_CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/boot_loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal_port.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/proto.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/boot_config.h
)

# ---- 静态库目标 ----
if(NOT TARGET lite_loader)
    add_library(lite_loader STATIC ${LOADER_CORE_SOURCES})
    target_include_directories(lite_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # 默认不定义 USING_ARM_CHIP（测试环境不需要）
    # 嵌入式项目应在自己的 CMake 中添加：
    #   target_compile_definitions(lite_loader PUBLIC USING_ARM_CHIP)
endif()
