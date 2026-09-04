# LiteLoader - 组件 CMake（嵌入式或被 test/CMakeLists.txt 引用）
#
# 提供目标：lite_loader（静态库）
#   - 嵌入式项目：直接 target_link_libraries(your_fw lite_loader)
#   - 测试构建：由 test/CMakeLists.txt 引入并链接

cmake_minimum_required(VERSION 3.15)

# ---- 核心源文件 ----
set(LOADER_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/loader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/proto_custom.c
)

set(LOADER_CORE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/loader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/port.h
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/proto_custom.h
    ${CMAKE_CURRENT_SOURCE_DIR}/conf.h
)

# ---- 静态库目标 ----
if(NOT TARGET lite_loader)
    add_library(lite_loader STATIC ${LOADER_CORE_SOURCES})
    target_include_directories(lite_loader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    # 默认不定义 USING_ARM_CHIP（测试环境不需要）
    # 嵌入式项目应在自己的 CMake 中添加：
    #   target_compile_definitions(lite_loader PUBLIC USING_ARM_CHIP)
endif()

# ---- 嵌入式使用示例（注释，供参考） ----
# 在你的嵌入式项目 CMakeLists.txt 中：
#
#   add_subdirectory(LiteLoader)
#   target_compile_definitions(lite_loader PUBLIC USING_ARM_CHIP)
#   target_link_libraries(your_firmware.elf lite_loader)
