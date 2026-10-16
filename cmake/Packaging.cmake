install(TARGETS pkzgui RUNTIME_DEPENDENCY_SET pkzgui_runtime RUNTIME DESTINATION .)
if(EXISTS "${PKZGUI_FFMPEG_EXECUTABLE}")
    add_executable(pkzgui_ffmpeg IMPORTED)
    set_target_properties(pkzgui_ffmpeg PROPERTIES IMPORTED_LOCATION "${PKZGUI_FFMPEG_EXECUTABLE}")
    install(IMPORTED_RUNTIME_ARTIFACTS pkzgui_ffmpeg
        RUNTIME_DEPENDENCY_SET pkzgui_runtime RUNTIME DESTINATION .)
endif()
if(WIN32)
    get_filename_component(PKZGUI_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(RUNTIME_DEPENDENCY_SET pkzgui_runtime
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
        DIRECTORIES "${PKZGUI_COMPILER_DIRECTORY}"
        RUNTIME DESTINATION . LIBRARY DESTINATION .)
endif()

install(FILES README.md DESTINATION docs)
install(DIRECTORY licenses/ DESTINATION licenses)
foreach(dependency imgui glfw nativefiledialog-extended miniaudio FFmpeg
        PKZLib/contrib/zlib PKZLib/thirdparty/textures/libsquish
        PKZLib/thirdparty/textures/rexglue PKZLib/thirdparty/textures/stb)
    file(GLOB dependency_licenses "${PROJECT_SOURCE_DIR}/external/${dependency}/LICENSE*"
        "${PROJECT_SOURCE_DIR}/external/${dependency}/COPYING*")
    install(FILES ${dependency_licenses} DESTINATION "licenses/${dependency}")
endforeach()

set(CPACK_PACKAGE_NAME pkzgui)
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_PACKAGE_FILE_NAME "pkzgui-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
