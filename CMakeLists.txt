project(gpgnet4ta)
cmake_minimum_required(VERSION 3.16)
set (CMAKE_CXX_STANDARD 11)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

# -------------- libcommuni submodule --------
set(ENABLE_IRC OFF CACHE BOOL "Enable IRC integration using libcommuni https://github.com/communi/libcommuni")
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

# -------------- taf/gpgnet4ta -----------------

if (WIN32)
    set(DIRECTX_SDK_DIR "" CACHE PATH "Path to Microsoft DirectX SDK (April 2007)")
    set(DIRECT_PLAY_INCLUDE_DIR "${DIRECTX_SDK_DIR}/Include")
    set(DIRECT_PLAY_LIB_DIR "${DIRECTX_SDK_DIR}/Lib/x86")
    set(DIRECT_PLAY_LIBS "dplayx;dxguid")

    add_subdirectory(libs/jdplay)
    add_subdirectory(apps/replayer)
    add_subdirectory(apps/talauncher)
ENDIF()

add_subdirectory(libs/gpgnet)
add_subdirectory(libs/talaunch)
add_subdirectory(libs/taflib)
add_subdirectory(libs/tafnet)
add_subdirectory(libs/tapacket)
add_subdirectory(libs/tareplay)
add_subdirectory(apps/gpgnet4ta)
add_subdirectory(apps/replayserver)
add_subdirectory(apps/testapp)
