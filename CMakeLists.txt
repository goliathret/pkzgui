cmake_minimum_required(VERSION 3.21)
project(pkzgui LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC)
endif()

foreach(dependency imgui/imgui.cpp glfw/CMakeLists.txt)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/${dependency}")
        message(FATAL_ERROR "Missing external/${dependency}: run git submodule update --init --recursive")
    endif()
endforeach()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
target_compile_features(imgui PUBLIC cxx_std_17)

set(PKZLIB_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(PKZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/PKZLib EXCLUDE_FROM_ALL)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
set(NFD_PORTAL ON CACHE BOOL "" FORCE)
add_subdirectory(external/nativefiledialog-extended EXCLUDE_FROM_ALL)

option(PKZGUI_BUILD_TESTS "Build package loading tests" OFF)
if(PKZGUI_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(src)
include(cmake/Packaging.cmake)
