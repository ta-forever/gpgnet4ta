set(CMAKE_AUTOMOC ON)

add_executable(replayer
    replayer.cpp
    TaReplayer.h
    TaReplayer.cpp
    )

target_include_directories(replayer 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${DIRECT_PLAY_INCLUDE_DIR}
    )

target_link_directories(replayer
    PUBLIC
    ${DIRECT_PLAY_LIB_DIR}
    )

target_link_libraries(replayer
    gpgnet
    jdplay
    taflib
    talaunch
    tapacket
    tareplay
    Qt5::Core
    Qt5::Network
    ${DIRECT_PLAY_LIBS}
    )

install(TARGETS replayer)
