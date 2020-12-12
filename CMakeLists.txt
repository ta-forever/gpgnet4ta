project(gpgnet4ta)
cmake_minimum_required(VERSION 3.16)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

set(DIRECTX_SDK_DIR "" CACHE PATH "Path to Microsoft DirectX SDK (April 2007)")
set(DIRECT_PLAY_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
set(DIRECT_PLAY_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")
set(DIRECT_PLAY_LIBS "dplayx;dxguid")

set(MINIUPNP_DIR "" CACHE PATH "Path to MiniUPnp binaries http://miniupnp.free.fr/files/")
set(MINIUPNP_INCLUDE_DIR "${MINIUPNP_DIR}/include")
set(MINIUPNP_LIB_DIR "${MINIUPNP_DIR}/lib")
set(MINIUPNP_LIBS_STATIC "debug;libminiupnpcd;optimized;libminiupnpc")

set(LIBCOMMUNI_DIR "" CACHE PATH "Path to libcommuni binaries https://github.com/communi/libcommuni")
set(LIBCOMMUNI_INCLUDE_DIR "${LIBCOMMUNI_DIR}/include/IrcCore;${LIBCOMMUNI_DIR}/include/IrcModel;${LIBCOMMUNI_DIR}/include/IrcUtil")
set(LIBCOMMUNI_LIB_DIR "${LIBCOMMUNI_DIR}/lib")
set(LIBCOMMUNI_LIBS_STATIC "debug;communid;optimized;communi")


add_subdirectory(jdplay)
add_subdirectory(tademo)
add_subdirectory(gpgnet)
add_subdirectory(tafnet)

# -------------- TADUMPER -------------

add_executable(tadumper
    tadumper.cpp)

target_link_libraries(tadumper
    tademo)


# -------------- TAPROXYTEST -------------

add_executable(taproxytest
    GameMonitor2.h
    GameMonitor2.cpp
    taproxytest.cpp)

target_link_directories(taproxytest
    PUBLIC
    ${MINIUPNP_INCLUDE_DIR})

target_link_libraries(taproxytest
    tademo
    tafnet
    Qt5::Core
    Qt5::Network
    ws2_32
    iphlpapi
    ${MINIUPNP_LIBS_STATIC})

# -------------- GPGNET4TA -------------

set(CMAKE_AUTOMOC ON)

add_executable(gpgnet4ta
    TaLobby.h
    TaLobby.cpp
    GameMonitor2.h
    GameMonitor2.cpp
    GameEventHandlerQt.h
    GameEventHandlerQt.cpp
    gpgnet4ta.cpp
    GpgNetGameLauncher.h
    GpgNetGameLauncher.cpp
	IrcForward.h
	IrcForward.cpp
    )

target_include_directories(gpgnet4ta 
    PUBLIC
    ${MINIUPNP_INCLUDE_DIR}
    ${LIBCOMMUNI_INCLUDE_DIR}
    )

target_link_directories(gpgnet4ta
    PUBLIC
    ${DIRECT_PLAY_LIB_DIR} 
    ${MINIUPNP_LIB_DIR}
    ${LIBCOMMUNI_LIB_DIR}
    )

target_link_libraries(gpgnet4ta
    jdplay
    tademo
    tafnet
    gpgnet
    Qt5::Core
    Qt5::Network
    ws2_32
    iphlpapi
    ${DIRECT_PLAY_LIBS}
    ${MINIUPNP_LIBS_STATIC}
    ${LIBCOMMUNI_LIBS_STATIC}
    )

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


# ----------------------

install(TARGETS gpgnet4ta tadumper)
