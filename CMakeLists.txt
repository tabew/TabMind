cmake_minimum_required(VERSION 3.20)
project(QQBotAI VERSION 2.0.0 LANGUAGES CXX)

# 本项目为 Win32 原生程序，仅支持 MSVC + Windows SDK，且不依赖任何第三方库
if(NOT MSVC)
    message(FATAL_ERROR "QQBotAI 使用 Win32 API（WinHTTP / Winsock / GDI），仅支持 Windows + MSVC 构建")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(/utf-8 /EHsc /W3)
add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(QQBotAI WIN32 ${SOURCES})

target_include_directories(QQBotAI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(QQBotAI PRIVATE
    winhttp
    ws2_32
    crypt32
    comctl32
    uxtheme
    dwmapi
    gdiplus
    msimg32
)

# 静态链接 C 运行时，产出单文件 exe，避免目标机缺 VC 运行库
set_property(TARGET QQBotAI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

# 若存在 config.json 则复制到输出目录（该文件不入库，见 .gitignore）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.json")
    add_custom_command(TARGET QQBotAI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:QQBotAI>/config.json"
        COMMENT "复制 config.json 到输出目录"
    )
endif()
