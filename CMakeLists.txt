cmake_minimum_required(VERSION 3.0)
project(mymuduo)

# 设置最终编译的动态库的路径, 放在根目录的 lib 文件夹下面
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

#  设置调试信息, 以及启动 C++ 11 语言标准进行编译
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -std=c++11")

# 定义参与编译的源代码文件
aux_source_directory(. SRC_LIST)
# 编译成动态库
add_library(mymuduo SHARED ${SRC_LIST}) 

# 定义宏
# add_compile_definitions(MUDEBUG) 