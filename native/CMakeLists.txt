cmake_minimum_required(VERSION 3.18)
project(rakcheat CXX)

include(FetchContent)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE BOOL "" FORCE)
set(Plugin.ImportTableReplace OFF CACHE BOOL "" FORCE)
set(DOBBY_GIT_TAG 5dfc8546954ce3b3198132ab13fddb89ee92cdd7)
FetchContent_Declare(dobby
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG ${DOBBY_GIT_TAG}
)
FetchContent_GetProperties(dobby)
if(NOT dobby_POPULATED)
    FetchContent_Populate(dobby)
endif()
execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/patch_dobby.py ${dobby_SOURCE_DIR}
    COMMAND_ERROR_IS_FATAL ANY
)
add_subdirectory(${dobby_SOURCE_DIR} ${dobby_BINARY_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rakcheat SHARED
    src/main.cpp
    src/hook.cpp
    src/render.cpp
    src/ui.cpp
    src/net.cpp
    src/packets.cpp
)

target_compile_options(rakcheat PRIVATE -O2 -fno-stack-protector -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter)
target_compile_definitions(rakcheat PRIVATE RC_USE_DOBBY=1)
target_include_directories(rakcheat PRIVATE ${dobby_SOURCE_DIR}/include)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(dl-lib dl)

target_link_libraries(rakcheat
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles-lib}
    ${dl-lib}
    dobby_static
)

# JNI_OnLoad must stay visible
set_target_properties(rakcheat PROPERTIES LINK_FLAGS "-Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,--strip-all")
