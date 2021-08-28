set(CMAKE_AUTOMOC ON)

if (WIN32)
    add_library(talaunch STATIC
        LaunchClient.h
        LaunchClient.cpp
        LaunchServer.h
        LaunchServer.cpp
    )

else()
    add_library(talaunch STATIC
        LaunchClient.h
        LaunchClient.cpp
    )

endif()

target_link_libraries(talaunch
    Qt5::Core
    Qt5::Network
    )

target_include_directories(talaunch 
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${DIRECT_PLAY_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
