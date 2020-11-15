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
set(MINIUPNP_LIBS_DYNAMIC "miniupnpc")
set(MINIUPNP_LIBS_STATIC "libminiupnpc")

# -------------- GPGNET4TA -------------

add_executable(gpgnet4ta
    gpgnet4ta.cpp)

target_include_directories(gpgnet4ta 
    PUBLIC
    ${MINIUPNP_INCLUDE_DIR}
)

target_link_directories(gpgnet4ta
    PUBLIC
	${DIRECT_PLAY_LIB_DIR} 
	${MINIUPNP_LIB_DIR})

target_link_libraries(gpgnet4ta
    jdplay
    tademo
    gpgnet
    Qt5::Core
    Qt5::Network
	ws2_32
	iphlpapi
    ${DIRECT_PLAY_LIBS}
	${MINIUPNP_LIBS_STATIC})

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


# -------------- TADUMPER -------------

add_executable(tadumper
    tadumper.cpp)

target_link_libraries(tadumper
    tademo)


# -------------- TAPROXY -------------

add_executable(taproxy
	taproxy.cpp)

target_link_directories(taproxy
    PUBLIC)

target_link_libraries(taproxy
	tademo
	tafnet
    Qt5::Core
    Qt5::Network)

# ----------------------

add_subdirectory(jdplay)
add_subdirectory(tademo)
add_subdirectory(gpgnet)
add_subdirectory(tafnet)

install(TARGETS gpgnet4ta tadumper taproxy)
