project(gpgnet4ta)
cmake_minimum_required(VERSION 3.16)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

#if(MSVC)
#    add_compile_options(
#        $<$<CONFIG:>:/MT> #---------|
#        $<$<CONFIG:Debug>:/MTd> #---|-- Statically link the runtime libraries
#        $<$<CONFIG:Release>:/MT> #--|
#    )
#endif()

if (WIN32)
set(DIRECTX_SDK_DIR "" CACHE PATH "Path to Microsoft DirectX SDK (April 2007)")
set(DIRECT_PLAY_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
set(DIRECT_PLAY_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")
set(DIRECT_PLAY_LIBS "dplayx;dxguid")

set(LIBCOMMUNI_DIR "" CACHE PATH "Path to libcommuni binaries https://github.com/communi/libcommuni")
set(LIBCOMMUNI_INCLUDE_DIR "${LIBCOMMUNI_DIR}/include/IrcCore;${LIBCOMMUNI_DIR}/include/IrcModel;${LIBCOMMUNI_DIR}/include/IrcUtil")
set(LIBCOMMUNI_LIB_DIR "${LIBCOMMUNI_DIR}/lib")
set(LIBCOMMUNI_LIBS_STATIC "debug;communid;optimized;communi")

else()
set(LIBCOMMUNI_DIR "" CACHE PATH "Path to libcommuni binaries https://github.com/communi/libcommuni")
set(LIBCOMMUNI_INCLUDE_DIR "${LIBCOMMUNI_DIR}/include/IrcCore;${LIBCOMMUNI_DIR}/include/IrcModel;${LIBCOMMUNI_DIR}/include/IrcUtil")
set(LIBCOMMUNI_LIB_DIR "${LIBCOMMUNI_DIR}/lib")
set(LIBCOMMUNI_LIBS_STATIC "IrcCore;IrcModel;IrcUtil")
  
endif()

add_subdirectory(tademo)
add_subdirectory(gpgnet)
add_subdirectory(tafnet)

# -------------- TALAUNCHER -------------

if (WIN32)

    add_subdirectory(jdplay)

    set(CMAKE_AUTOMOC ON)

    add_executable(talauncher
        TaLauncher.cpp
        LaunchServer.h
        LaunchServer.cpp
        Logger.h
        Logger.cpp
        MessageBoxThread.h
        MessageBoxThread.cpp
        )

    target_link_directories(talauncher
        PUBLIC
        ${DIRECT_PLAY_LIB_DIR} 
        )

    target_link_libraries(talauncher
        jdplay
        Qt5::Core
        Qt5::Network
        ws2_32
        #iphlpapi
        ${DIRECT_PLAY_LIBS}
        )

    install(TARGETS talauncher)

ENDIF()

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
    LaunchClient.h
    LaunchClient.cpp
    Logger.h
    Logger.cpp
    IrcForward.h
    IrcForward.cpp
    ConsoleReader.h
    ConsoleReader.cpp
    )

target_include_directories(gpgnet4ta 
    PUBLIC
    ${LIBCOMMUNI_INCLUDE_DIR}
    )

target_link_directories(gpgnet4ta
    PUBLIC
    ${LIBCOMMUNI_LIB_DIR}
    )

target_link_libraries(gpgnet4ta
    tademo
    tafnet
    gpgnet
    Qt5::Core
    Qt5::Network
    ${LIBCOMMUNI_LIBS_STATIC}
    )

add_custom_command(
    TARGET gpgnet4ta POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Qt5::Core>
        $<TARGET_FILE:Qt5::Network>
        $<TARGET_FILE_DIR:gpgnet4ta>)

install(TARGETS gpgnet4ta)
