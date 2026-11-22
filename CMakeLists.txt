cmake_minimum_required(VERSION 3.20)
project(Cinemania VERSION 1.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Cinemania requires Windows and the MSVC toolchain.")
endif()

option(CINEMANIA_BUILD_WORKER "Build the isolated x64 encoder worker" OFF)
if(CINEMANIA_BUILD_WORKER)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "CinemaniaWorker requires an x64 build.")
    endif()
    add_executable(CinemaniaWorker
        src/common/protocol.cpp
        src/worker/main.cpp
        src/worker/native_runtime.cpp
        src/worker/output_files.cpp
        src/worker/session_input.cpp
        src/worker/worker_session.cpp)
    target_compile_features(CinemaniaWorker PRIVATE cxx_std_20)
    target_compile_options(CinemaniaWorker PRIVATE /W4 /WX /permissive- /EHsc)
    target_compile_definitions(CinemaniaWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(CinemaniaWorker PRIVATE ole32)
    set_property(TARGET CinemaniaWorker PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "Cinemania requires an x86 (Win32) build.")
    endif()
    enable_language(C)
    add_library(CinemaniaMinHook STATIC
        third_party/minhook/src/buffer.c
        third_party/minhook/src/hook.c
        third_party/minhook/src/trampoline.c
        third_party/minhook/src/hde/hde32.c)
    target_include_directories(CinemaniaMinHook PUBLIC third_party/minhook/include)
    set_property(TARGET CinemaniaMinHook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_library(Cinemania SHARED
        src/dll/audio/audio_export.cpp
        src/dll/audio/export_volume.cpp
        src/dll/audio/export_volume_bridges.cpp
        src/dll/audio/mixer_pcm_buffer.cpp
        src/dll/audio/openal_capture.cpp
        src/dll/audio/openal_capture_hooks.cpp
        src/dll/bootstrap.cpp
        src/dll/bootstrap_diagnostics.cpp
        src/dll/game/executable_check.cpp
        src/dll/video/capture_session.cpp
        src/dll/encoder/encoder_client.cpp
        src/dll/encoder/encoder_scratch.cpp
        src/dll/encoder/encoder_transport.cpp
        src/dll/video/game_export.cpp
        src/dll/game/game_ui.cpp
        src/dll/replay/hd_replay.cpp
        src/dll/replay/hd_replay_file.cpp
        src/dll/replay/hd_replay_menu.cpp
        src/dll/video/video_export.cpp
        src/common/protocol.cpp
        src/dll/video/hook_callbacks.cpp
        src/dll/game/hook_transaction.cpp
        src/dll/passthrough_hooks.cpp
        src/dll/video/viewport_capture_hooks.cpp
        src/dll/video/native_render_ahead.cpp
        src/dll/cinemania.def
        src/dll/video/readback_ring.cpp
        src/dll/video/deferred_readback.cpp)

    target_include_directories(Cinemania PRIVATE src/dll)
    target_compile_features(Cinemania PRIVATE cxx_std_20)
    target_compile_options(Cinemania PRIVATE /W4 /WX /permissive- /EHsc)
    target_compile_definitions(Cinemania PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(Cinemania PRIVATE bcrypt shell32 ole32 CinemaniaMinHook)
    set_property(TARGET Cinemania PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
