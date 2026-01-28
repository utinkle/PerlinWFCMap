cmake_minimum_required(VERSION 3.16)
project(MapGenerator VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build as shared library" ON)
option(BUILD_QT_PREVIEW "Build Qt preview application" ON)

# 内部头文件
set(INTERNAL_HEADERS
    src/internal/CommonTypes.h
    src/internal/NoiseGenerator.h
    src/internal/WFCGenerator.h
    src/internal/MapGeneratorInternal.h
    src/internal/ThreadPool.h
)

# 源文件
set(SOURCES
    src/MapGenerator.cpp
    src/internal/MapGeneratorInternal.cpp
    src/internal/NoiseGenerator.cpp
    src/internal/WFCGenerator.cpp
    src/internal/ThreadPool.cpp
)

# 主库
add_library(MapGenerator
    ${SOURCES}
)

target_include_directories(MapGenerator
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
)

# 编译选项：为 MSVC 强制使用 UTF-8 源文件编码
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 设置导出宏（只定义 MG_BUILD_LIB 给库自身，头文件负责 MG_EXPORT）
if(WIN32)
    target_compile_definitions(MapGenerator PRIVATE MG_BUILD_LIB)
else()
    target_compile_definitions(MapGenerator PRIVATE MG_BUILD_LIB)
endif()

# 安装规则
install(TARGETS MapGenerator
    EXPORT MapGeneratorTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT MapGeneratorTargets
    FILE MapGeneratorConfig.cmake
    NAMESPACE MapGenerator::
    DESTINATION lib/cmake/MapGenerator
)

install(FILES include/MapGenerator.h
    DESTINATION include
)

# Qt预览应用
if(BUILD_QT_PREVIEW)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)

    set(TRANSLATION_FILES
        qt_preview/translation/mapgenerator_en.ts
        qt_preview/translation/mapgenerator_zh_CN.ts
    )
    
    qt_add_translation(QM_FILES ${TRANSLATION_FILES})

    qt_add_resources(tranlation_files qt_preview/translation/translation.qrc)

    qt_add_executable(qt_preview
        qt_preview/main.cpp
        qt_preview/MainWindow.cpp
        qt_preview/MainWindow.h
        qt_preview/MapView.cpp
        qt_preview/MapView.h
        qt_preview/ConfigPanel.cpp
        qt_preview/ConfigPanel.h
        ${tranlation_files}
    )

    target_link_libraries(qt_preview
        PRIVATE
            MapGenerator
            Qt6::Widgets
    )
endif()

# 示例
add_executable(example examples/example.cpp)
target_link_libraries(example MapGenerator)
