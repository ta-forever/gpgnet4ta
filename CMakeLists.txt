project(gpgnet4ta)
cmake_minimum_required(VERSION 3.16)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

set(DIRECTX_SDK_DIR "" CACHE PATH "Path to Microsoft DirectX SDK (April 2007)")
set(DIRECT_PLAY_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
set(DIRECT_PLAY_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")
set(DIRECT_PLAY_LIBS "dplayx;dxguid")

add_executable(gpgnet4ta
    gpgnet4ta.cpp)

target_link_libraries(gpgnet4ta
    jdplay
    tademo
    Qt5::Core
    Qt5::Network
    ${DIRECT_PLAY_LIBS})

target_link_directories(gpgnet4ta
    PUBLIC ${DIRECT_PLAY_LIB_DIR})

add_executable(tadumper
    tadumper.cpp)

target_link_libraries(tadumper
    tademo)

add_subdirectory(jdplay)
add_subdirectory(tademo)
