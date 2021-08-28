set(CMAKE_AUTOMOC ON)

add_library(tafnet STATIC
    GameAddressTranslater.h
    GameAddressTranslater.cpp
    GameReceiver.h
    GameReceiver.cpp
    GameSender.h
    GameSender.cpp
    TafnetGameNode.h
    TafnetGameNode.cpp
    TafnetNode.h
    TafnetNode.cpp
    )

target_link_libraries(tafnet
    Qt5::Core
    Qt5::Network
    taflib
    )

target_include_directories(tafnet 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_CURRENT_SOURCE_DIR})
