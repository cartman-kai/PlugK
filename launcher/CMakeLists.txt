cmake_minimum_required(VERSION 3.10)
project(MyProject)

# 1. 自动搜集所有源代码文件
file(GLOB_RECURSE ALL_SOURCES 
    "src/*.cpp"
    "include/*.h"
    "dep/imgui/*.cpp"
    "dep/imgui/*.h"
)

# 2. 核心魔法：根据物理目录自动创建 VS 过滤器
# 这一段代码会遍历所有文件，把它们在硬盘上的相对路径映射为 VS 的筛选器路径
foreach(FILE ${ALL_SOURCES}) 
    get_filename_component(DIR ${FILE} DIRECTORY)
    file(RELATIVE_PATH REL_DIR ${CMAKE_CURRENT_SOURCE_DIR} ${DIR})
    string(REPLACE "/" "\\" REL_DIR "${REL_DIR}") # 将路径分隔符转为反斜杠
    source_group("${REL_DIR}" FILES ${FILE})
endforeach()

# 3. 创建可执行程序
add_executable(MyProject ${ALL_SOURCES})

# 4. 设置包含目录
target_include_directories(MyProject PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/dep/imgui"
)