cmake_minimum_required(VERSION 3.15)
project(Direct3D12Renderer)

# 指定 vcpkg 的工具链文件
set(CMAKE_TOOLCHAIN_FILE "D:/Personal Project/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "" FORCE)
set(CMAKE_PREFIX_PATH "D:/Personal Project/vcpkg/installed/x64-windows/share" CACHE STRING "Path to vcpkg installed packages" FORCE)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SYSTEM_VERSION "10.0.22621.0")

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories("C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um")
include_directories("C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared")
include_directories("C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/winrt")
link_directories("C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64")

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
file(GLOB SHADERS ${CMAKE_SOURCE_DIR}/Shaders/*.hlsl)
file(GLOB SHADERS ${CMAKE_SOURCE_DIR}/include/*.h)

# 添加可执行文件
add_definitions(-DUNICODE -D_UNICODE)
add_executable(Direct3D12Renderer WIN32 src/main.cpp src/Renderer.cpp src/FrameResource.cpp
                                        src/d3dUtil.cpp src/MathHelper.cpp src/Camera.cpp
                                        src/GeometryGenerator.cpp)

# 使用 vcpkg 安装的库
find_package(tinyobjloader CONFIG REQUIRED)

target_link_libraries(Direct3D12Renderer 
    d3d12 
    dxgi 
    d3dcompiler
    dxguid)
target_link_libraries(Direct3D12Renderer tinyobjloader::tinyobjloader)
