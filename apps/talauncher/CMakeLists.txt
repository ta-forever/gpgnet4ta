add_executable(talauncher
    TaLauncher.cpp
    )

target_include_directories(talauncher 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${LIBCOMMUNI_INCLUDE_DIR}
    )

target_link_directories(talauncher
    PUBLIC
    ${DIRECT_PLAY_LIB_DIR}
    )

target_link_libraries(talauncher
    taflib
    talaunch
    jdplay
    Qt5::Core
    Qt5::Network
    ${DIRECT_PLAY_LIBS}
    )

install(TARGETS talauncher)
