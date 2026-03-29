set(CMAKE_AUTOMOC ON)

if (WIN32)

    add_library(talaunch STATIC
        LaunchClient.h
        LaunchClient.cpp
        LaunchServer.h
        LaunchServer.cpp
    )

    target_link_libraries(talaunch
        Qt5::Core
        Qt5::Network
        tafencrypt
        rwe
        dplayreg
        jdplay
    )

else()
    add_library(talaunch STATIC
        LaunchClient.h
        LaunchClient.cpp
    )

    target_link_libraries(talaunch
        Qt5::Core
        Qt5::Network
    )

endif()

target_include_directories(talaunch 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${DIRECT_PLAY_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
