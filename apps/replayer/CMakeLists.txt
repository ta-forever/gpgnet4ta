add_executable(tareplayer
    TaReplayer.cpp
    )

target_include_directories(tareplayer 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    )

target_link_directories(tareplayer
    PUBLIC
    ${DIRECT_PLAY_LIB_DIR}
    )

target_link_libraries(tareplayer
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

install(TARGETS tareplayer)
