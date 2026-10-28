set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(READ "${IMGUI_DIR}/misc/fonts/DroidSans.ttf" PKZGUI_FONT_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," PKZGUI_FONT_BYTES "${PKZGUI_FONT_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pkzgui_font.h"
    "static const unsigned char FontData[] = {${PKZGUI_FONT_BYTES}};\n")

add_library(miniaudio STATIC audio/miniaudio_impl.c)
target_include_directories(miniaudio PUBLIC "${PROJECT_SOURCE_DIR}/external/miniaudio")
target_link_libraries(miniaudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(miniaudio PRIVATE m)
endif()

set(PKZGUI_FFMPEG_EXECUTABLE "${PROJECT_SOURCE_DIR}/external/FFmpeg/ffmpeg${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "FFmpeg executable built alongside pkzgui")

if(APPLE OR WIN32)
    set(NATIVE_MENU_DIR "${PROJECT_SOURCE_DIR}/external/native-menu-bar")
    if(NOT EXISTS "${NATIVE_MENU_DIR}/native_menu_bar.c")
        message(FATAL_ERROR "Missing native-menu-bar: run git submodule update --init --recursive")
    endif()
    if(APPLE)
        set_source_files_properties("${NATIVE_MENU_DIR}/native_menu_bar.c"
            PROPERTIES LANGUAGE OBJC COMPILE_OPTIONS "-fno-objc-arc")
    else()
        set_source_files_properties("${NATIVE_MENU_DIR}/native_menu_bar.c" PROPERTIES LANGUAGE CXX)
    endif()
    add_library(native_menu_bar STATIC "${NATIVE_MENU_DIR}/native_menu_bar.c")
    target_include_directories(native_menu_bar PUBLIC "${NATIVE_MENU_DIR}")
    if(APPLE)
        target_link_libraries(native_menu_bar PRIVATE "-framework Cocoa")
    else()
        target_compile_features(native_menu_bar PRIVATE cxx_std_17)
        target_link_libraries(native_menu_bar PRIVATE user32)
    endif()
endif()

add_library(pkzgui_lib STATIC
    animation/animation_placement.cpp
    animation/rig_mapping.cpp
    animation/model_animation.cpp
    app/app_menu.cpp
    app/application.cpp
    audio/audio.cpp
    hud/hud_preview.cpp
    model/gltf_export.cpp
    model/model_controls.cpp
    model/model_preview.cpp
    model/model_renderer.cpp
    model/orbit_camera.cpp
    package/package_search.cpp
    package/package_session.cpp
    platform/platform.cpp
    render/gl.cpp
    texture/texture.cpp
    ui/widgets.cpp
    ui/workspace.cpp
    ui/workspace_commands.cpp)
target_include_directories(pkzgui_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(pkzgui_lib PUBLIC pkzlib imgui nfd miniaudio Threads::Threads)
target_compile_features(pkzgui_lib PUBLIC cxx_std_17)
target_compile_definitions(pkzgui_lib
    PUBLIC GL_SILENCE_DEPRECATION
    PRIVATE PKZGUI_FFMPEG_PATH="${PKZGUI_FFMPEG_EXECUTABLE}")
if(APPLE OR WIN32)
    target_compile_definitions(pkzgui_lib PRIVATE PKZGUI_NATIVE_MENUS)
    target_link_libraries(pkzgui_lib PUBLIC native_menu_bar)
endif()
if(APPLE)
    target_sources(pkzgui_lib PRIVATE app/menu_shortcut.m)
endif()

add_executable(pkzgui main.cpp)
target_link_libraries(pkzgui PRIVATE pkzgui_lib)
set_target_properties(pkzgui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(EXISTS "${PKZGUI_FFMPEG_EXECUTABLE}")
    add_custom_command(TARGET pkzgui POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PKZGUI_FFMPEG_EXECUTABLE}"
            "$<TARGET_FILE_DIR:pkzgui>/ffmpeg${CMAKE_EXECUTABLE_SUFFIX}")
endif()

if(PKZGUI_BUILD_TESTS)
    function(pkzgui_test name)
        add_executable(${name} "${PROJECT_SOURCE_DIR}/tests/${name}.cpp")
        target_link_libraries(${name} PRIVATE pkzgui_lib)
    endfunction()

    pkzgui_test(package_session_test)
    add_test(NAME package_session COMMAND package_session_test "${CMAKE_CURRENT_BINARY_DIR}/test-packages")

    pkzgui_test(package_search_test)
    add_test(NAME package_search COMMAND package_search_test)

    pkzgui_test(texture_actions_test)
    add_test(NAME texture_actions COMMAND texture_actions_test "${CMAKE_CURRENT_BINARY_DIR}/test-textures")

    pkzgui_test(audio_sample_test)
    add_test(NAME audio_sample COMMAND audio_sample_test)

    pkzgui_test(animation_test)
    add_test(NAME animation COMMAND animation_test)

    pkzgui_test(file_dialog_test)
    add_test(NAME file_dialog COMMAND file_dialog_test "${CMAKE_CURRENT_BINARY_DIR}/test-dialogs")

    pkzgui_test(model_preview_test)
    add_test(NAME model_selection COMMAND model_preview_test)
    add_test(NAME model_render COMMAND model_preview_test --gl)
    set_tests_properties(model_render PROPERTIES SKIP_RETURN_CODE 77)
endif()
