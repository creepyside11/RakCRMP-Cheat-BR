cmake_minimum_required(VERSION 3.18)
project(rakcheat CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rakcheat SHARED
    src/main.cpp
    src/hook.cpp
    src/render.cpp
    src/ui.cpp
    src/net.cpp
)

target_compile_options(rakcheat PRIVATE -O2 -fno-stack-protector -fno-exceptions -fno-rtti -Wall -Wno-unused-parameter)

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
)

# JNI_OnLoad must stay visible
set_target_properties(rakcheat PROPERTIES LINK_FLAGS "-Wl,--no-undefined -Wl,-z,max-page-size=16384")
