add_executable(replayserver
    ReplayServer.cpp
    )

target_include_directories(replayserver 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    )

target_link_libraries(replayserver
    tareplay
    Qt5::Core
    Qt5::Network
    )

install(TARGETS replayserver)
