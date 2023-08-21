set(CMAKE_AUTOMOC ON)

add_executable(testapp
    testapp.cpp
    )

target_include_directories(testapp
    PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    )

target_link_directories(testapp
    PUBLIC
    )

target_link_libraries(testapp
    tapacket
    )

install(TARGETS testapp)
