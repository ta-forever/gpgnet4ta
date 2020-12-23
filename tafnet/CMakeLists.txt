set(MINIUPNP_DIR "" CACHE PATH "Path to MiniUPnp binaries http://miniupnp.free.fr/files/")
set(MINIUPNP_INCLUDE_DIR "${MINIUPNP_DIR}/include")
set(MINIUPNP_LIB_DIR "${MINIUPNP_DIR}/lib")
set(MINIUPNP_LIBS_STATIC "debug;libminiupnpcd;optimized;libminiupnpc")

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
    GetUpnpPortMap.h
    GetUpnpPortMap.cpp)

target_link_directories(tafnet
    PUBLIC
	${MINIUPNP_LIB_DIR})

target_link_libraries(tafnet
    Qt5::Core
    ${MINIUPNP_LIBS_STATIC})

target_include_directories(tafnet 
    PUBLIC
	${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MINIUPNP_INCLUDE_DIR})
