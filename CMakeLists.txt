project(gpgnet4ta)
cmake_minimum_required(VERSION 3.16)
set (CMAKE_CXX_STANDARD 11)

find_package(Qt5 COMPONENTS Core Network REQUIRED)


if (WIN32)

    set(DIRECTX_SDK_DIR "" CACHE PATH "Path to Microsoft DirectX SDK (April 2007)")
    set(DIRECT_PLAY_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
    set(DIRECT_PLAY_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")
    set(DIRECT_PLAY_LIBS "dplayx;dxguid")

    add_subdirectory(jdplay)

    # -------------- REPLAYER ---------------
    add_executable(tareplayer
        TaReplayer.cpp
        Logger.h
        Logger.cpp
        )

    target_include_directories(tareplayer 
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DIRECT_PLAY_INCLUDE_DIR})

    target_link_directories(tareplayer
        PUBLIC
        ${DIRECT_PLAY_LIB_DIR} 
        )

    target_link_libraries(tareplayer
        jdplay
        tademo
        Qt5::Core
        Qt5::Network
        ws2_32
        ${DIRECT_PLAY_LIBS}
        )

    install(TARGETS tareplayer)

    # -------------- TALAUNCHER -------------

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
        ${DIRECT_PLAY_LIBS}
        )

    install(TARGETS talauncher)

ENDIF()

# -------------- libcommuni submodule --------
set(ENABLE_IRC ON CACHE BOOL "Enable IRC integration using libcommuni https://github.com/communi/libcommuni")
set(LIBCOMMUNI_DIR "${CMAKE_BINARY_DIR}/libcommuni" CACHE PATH "Path to libcommuni binaries https://github.com/communi/libcommuni")

if (ENABLE_IRC)
    file(MAKE_DIRECTORY ${LIBCOMMUNI_DIR})

    get_target_property (QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
    add_custom_target(libcommuni ALL
      COMMAND ${QT_QMAKE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/libcommuni WORKING_DIRECTORY ${LIBCOMMUNI_DIR}
      COMMAND make WORKING_DIRECTORY ${LIBCOMMUNI_DIR}
      )

    set(LIBCOMMUNI_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/libcommuni/include/IrcCore;${CMAKE_SOURCE_DIR}/libcommuni/include/IrcModel;${CMAKE_SOURCE_DIR}/libcommuni/include/IrcUtil")
    set(LIBCOMMUNI_LIB_DIR "${LIBCOMMUNI_DIR}/lib")
    set(LIBCOMMUNI_LIBS_STATIC "IrcCore;IrcModel;IrcUtil")

    add_definitions(-D_ENABLE_IRC)
endif()

# -------------- GPGNET4TA -------------

add_subdirectory(tademo)
add_subdirectory(gpgnet)
add_subdirectory(tafnet)

if (ENABLE_IRC)
  set(IRC_SOURCES IrcForward.h;IrcForward.cpp)
else()
  set(IRC_SOURCES IrcForwardMock.h;IrcForwardMock.cpp)
endif()

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
    ConsoleReader.h
    ConsoleReader.cpp
    ${IRC_SOURCES}
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
 
install(TARGETS gpgnet4ta)
