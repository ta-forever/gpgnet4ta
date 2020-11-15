add_library(tafnet
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
    Qt5::Core)

target_include_directories(tafnet 
    PUBLIC
	${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
