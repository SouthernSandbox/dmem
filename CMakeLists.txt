# CMakeLists.txt 开头添加（必须在 project() 之前！）
# 1. 强制指定 C/C++ 编译器为 Clang（替换为你的实际路径）
# set(CMAKE_C_COMPILER "gcc.exe" CACHE FILEPATH "C compiler" FORCE)
# set(CMAKE_CXX_COMPILER "g++.exe" CACHE FILEPATH "C++ compiler" FORCE)

# 2. 强制指定 Make 工具路径
# set(CMAKE_MAKE_PROGRAM "make.exe" CACHE FILEPATH "Make program" FORCE)

# 3. 禁用编译器自动检测（防止 CMake 绕开你的设置去查 GCC）
# set(CMAKE_C_COMPILER_WORKS ON CACHE BOOL "" FORCE)
# set(CMAKE_CXX_COMPILER_WORKS ON CACHE BOOL "" FORCE)

# 后续的其他配置（如 C 标准、源文件、输出目录等）
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)


# 原有的 project 命令（确保在上述设置之后）
cmake_minimum_required(VERSION 3.10)
project(main)  # 你的 project 命令，注意名称匹配

# —— 核心：自动递归处理所有源文件和头文件 ——
# 1. 定义需要排除的目录（避免编译输出、Git 等无关目录被错误扫描）
set(EXCLUDE_DIRS 
    "${PROJECT_SOURCE_DIR}/bin"         # 可执行文件输出目录
    "${PROJECT_SOURCE_DIR}/build"       # 编译中间文件目录（若用 out-of-source 编译）
    "${PROJECT_SOURCE_DIR}/.git"        # Git 版本控制目录（若存在）
    "${PROJECT_SOURCE_DIR}/.vscode"     # VSCode 配置目录（若存在）
    "${PROJECT_SOURCE_DIR}/documents"   # 文档目录（若存在）
)

# 2. 递归查找所有源文件（.c + .cpp），并排除 EXCLUDE_DIRS
file(GLOB_RECURSE ALL_SOURCES 
    LIST_DIRECTORIES false  # 只抓文件，不抓目录
    "${PROJECT_SOURCE_DIR}/*.c"
    "${PROJECT_SOURCE_DIR}/*.cpp"
)
foreach(EXCLUDE ${EXCLUDE_DIRS})
    list(FILTER ALL_SOURCES EXCLUDE REGEX "${EXCLUDE}.*")  # 过滤排除目录下的文件
endforeach()

# 3. 定义一个空列表用于存储所有子目录
set(ALL_INCLUDE_DIRS "")

# 递归查找 project 目录下的所有子目录
function(find_all_subdirectories current_dir)
    file(GLOB children RELATIVE ${current_dir} ${current_dir}/*)
    message("[ Searching in directory ]:   ${current_dir}")
    foreach(child ${children})
        set(full_path ${current_dir}/${child})
        if(IS_DIRECTORY ${full_path})
            message("[ Found subdirectory     ]:   ${full_path}")
            list(APPEND ALL_INCLUDE_DIRS ${full_path})
            find_all_subdirectories(${full_path})
        endif()
    endforeach()
    # 将 ALL_INCLUDE_DIRS 列表传递回父作用域，确保最终的目录列表能够正确传递。
    set(ALL_INCLUDE_DIRS ${ALL_INCLUDE_DIRS} PARENT_SCOPE)
endfunction()

# 从 project 目录开始查找
find_all_subdirectories(${CMAKE_CURRENT_SOURCE_DIR}/project)

# 添加 project 根目录和所有子目录到头文件搜索路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/project ${ALL_INCLUDE_DIRS})

# 打印所有包含目录，方便调试
message("All include directories: ${ALL_INCLUDE_DIRS}")

# 将生成的可执行文件保存到指定目录中，其中：
#   1. ${PROJECT_SOURCE_DIR} 由 cmake 维护的变量，表示整个项目的根目录
#   2. EXECUTABLE_OUTPUT_PATH 是由 cmake 维护的变量，指定最终的可执行文件的位置
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# —— 生成可执行文件 ——
add_executable(main ${ALL_SOURCES})  # 把所有递归找到的源文件加入编译

# 自动添加所有头文件目录（写 #include 时无需手动指定子目录）
target_include_directories(main PRIVATE ${INCLUDE_DIRS})