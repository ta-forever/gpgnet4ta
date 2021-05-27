set(CMAKE_AUTOMOC ON)

add_library(tafnet STATIC
    TafnetNode.h
    TafnetNode.cpp
    TafnetGameNode.h
    TafnetGameNode.cpp
    GameSender.h
    GameSender.cpp
    GameReceiver.h
    GameReceiver.cpp
    GameAddressTranslater.h
    GameAddressTranslater.cpp
    )

target_link_libraries(tafnet
    Qt5::Core
    Qt5::Network
    tademo
    )

target_include_directories(tafnet 
    PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}})
