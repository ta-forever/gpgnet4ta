set(CMAKE_AUTOMOC ON)

add_executable(replayserver
    replayserver.cpp
    TafLobbyClient.h
    TafLobbyClient.cpp
    TafLobbyJsonProtocol.h
    TafLobbyJsonProtocol.cpp
    TafHwIdGenerator.h
    TafHwIdGenerator.cpp
    )

target_include_directories(replayserver 
    PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs
    )

target_link_libraries(replayserver
    tareplay
    Qt5::Core
    Qt5::Network
    )

install(TARGETS replayserver)
