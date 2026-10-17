cmake_minimum_required(VERSION 3.22.1)

project("xposedmenu")

aux_source_directory(libraries/ImGui imgui-src)
aux_source_directory(libraries/KittyMemory/KittyMemory kittymemory-src)

add_subdirectory(libraries/Dobby EXCLUDE_FROM_ALL)
add_subdirectory(libraries/BNM-Android EXCLUDE_FROM_ALL)
get_property(BNM_INCLUDE_DIRECTORIES TARGET BNM PROPERTY BNM_INCLUDE_DIRECTORIES)

add_library(
        ${CMAKE_PROJECT_NAME}
        SHARED
        ${imgui-src}
        ${kittymemory-src}
        libraries/ImGui/backends/imgui_impl_opengl3.cpp
        libraries/ImGui/backends/imgui_impl_android.cpp
        main.cpp
        poco_tree.cpp
        pet_egg_hooks.cpp
        poco_runner.cpp
        poco_server.cpp
)

include_directories(
        include
        Game
        libraries/ImGui
        libraries/ImGui/backends
        ${BNM_INCLUDE_DIRECTORIES}
        libraries/KittyMemory/KittyMemory
        libraries/KittyMemory/KittyMemory/Deps/Keystone/includes
        libraries/Dobby/include
        compiled_libs/curl/${CMAKE_ANDROID_ARCH_ABI}/include/curl
        compiled_libs/openssl/${CMAKE_ANDROID_ARCH_ABI}/include/openssl
)

set(CURL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/compiled_libs/curl/${CMAKE_ANDROID_ARCH_ABI}/lib/libcurl.a)
set(OPENSSL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/compiled_libs/openssl/${CMAKE_ANDROID_ARCH_ABI}/lib/libssl.a)
set(CRYPTO_LIB ${CMAKE_CURRENT_SOURCE_DIR}/compiled_libs/openssl/${CMAKE_ANDROID_ARCH_ABI}/lib/libcrypto.a)
set(KEYSTONE_LIB
    ${CMAKE_CURRENT_SOURCE_DIR}/libraries/KittyMemory/KittyMemory/Deps/Keystone/libs-android/${CMAKE_ANDROID_ARCH_ABI}/libkeystone.a
)
find_library(egl EGL)
find_library(GLESV2_LIB GLESv2)
find_library(LZ_LIB z)

if (${ANDROID_PLATFORM_LEVEL} LESS 12)
    message(FATAL_ERROR "OpenGL 2 is not supported before API level 11 /
                      (currently using ${ANDROID_PLATFORM_LEVEL}).")
    return()
elseif (${ANDROID_PLATFORM_LEVEL} LESS 18)
    add_definitions("-DDYNAMIC_ES3")
    set(GL3STUB_SRC gl3stub.c)
    set(OPENGL_LIB GLESv2)
else ()
    set(OPENGL_LIB GLESv3)
endif (${ANDROID_PLATFORM_LEVEL} LESS 12)

target_link_libraries(
        ${CMAKE_PROJECT_NAME}
        ${CURL_LIB}
        ${OPENSSL_LIB}
        ${CRYPTO_LIB}
        ${egl}
        ${GLESV2_LIB}
        ${LZ_LIB}
        ${OPENGL_LIB}
        BNM
        ${KEYSTONE_LIB}
        dobby
        android
        log
)
target_include_directories(BNM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libraries/Dobby/include)