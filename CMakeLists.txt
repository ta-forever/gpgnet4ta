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
    gpgnet
    Qt5::Core
    Qt5::Network
    ${DIRECT_PLAY_LIBS})

#add_custom_command(
#    TARGET gpgnet4ta POST_BUILD
#    COMMAND "${Qt5_DIR}/../../../bin/windeployqt.exe"
#    ARGS "$<TARGET_FILE:gpgnet4ta>")
add_custom_command(
    TARGET gpgnet4ta POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt5::Core>
        $<TARGET_FILE:Qt5::Network>
        $<TARGET_FILE_DIR:gpgnet4ta>)

target_link_directories(gpgnet4ta
    PUBLIC ${DIRECT_PLAY_LIB_DIR})

add_executable(tadumper
    tadumper.cpp)

target_link_libraries(tadumper
    tademo)

add_subdirectory(jdplay)
add_subdirectory(tademo)
add_subdirectory(gpgnet)

install(TARGETS gpgnet4ta tadumper)
