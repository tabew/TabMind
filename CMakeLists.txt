cmake_minimum_required(VERSION 3.16)
project(TabMind VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 同一套源码，两个构建目标：
#   Windows + MSVC  → GUI 单窗口程序（Win32 / GDI / WinHTTP）
#   Linux + GCC     → 无头（headless）服务端程序（POSIX socket / libcurl）
# 差异全部收口在 src/platform/ 与 include/{simple_http,simple_ws}.hpp 里。
# ---------------------------------------------------------------------------

if(MSVC)
    add_compile_options(/utf-8 /EHsc /W3)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
    add_compile_options(-O2 -Wall -Wextra -pthread)
endif()

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(MSVC)
    set(SOURCES ${ALL_SOURCES})
    # Windows 目标不需要 POSIX 平台实现
    list(FILTER SOURCES EXCLUDE REGEX "platform_linux\\.cpp$")
else()
    # Linux 目标没有图形环境，整个 UI 层不参与编译
    set(SOURCES ${ALL_SOURCES})
    list(FILTER SOURCES EXCLUDE REGEX "/src/ui/")
    list(FILTER SOURCES EXCLUDE REGEX "platform_win\\.cpp$")
endif()

if(MSVC)
    add_executable(TabMind WIN32 ${SOURCES})
else()
    add_executable(TabMind ${SOURCES})
endif()

target_include_directories(TabMind PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_link_libraries(TabMind PRIVATE
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
    set_property(TARGET TabMind PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
    find_package(Threads REQUIRED)
    find_package(CURL REQUIRED)
    target_link_libraries(TabMind PRIVATE Threads::Threads CURL::libcurl ${CMAKE_DL_LIBS})
    # 插件用 dlopen 加载，需要能解析主程序里的符号（simplejson 等 header-only 实现）
    target_link_options(TabMind PRIVATE -rdynamic)
endif()

# 若存在 config.json 则复制到输出目录（该文件不入库，见 .gitignore）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config.json")
    add_custom_command(TARGET TabMind POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
            "$<TARGET_FILE_DIR:TabMind>/config.json"
        COMMENT "复制 config.json 到输出目录"
    )
endif()
