SET(CMAKE_SYSTEM_NAME Linux)

IF ( "${PLATFORM_CHIP}" STREQUAL "lg1154" )
	SET(CMAKE_C_COMPILER arm-lg115x-linux-gnueabi-gcc)
ENDIF ()

IF ( "${TARGET_PLATFORM_NAME}" STREQUAL "webos-h13" )
	SET(CMAKE_C_COMPILER arm-lg115x-linux-gnueabi-gcc)
ENDIF ()


MESSAGE ( STATUS "selected compiler ==> " ${PLATFORM_CHIP} ":" ${TARGET_PLATFORM_NAME} ":" ${CMAKE_C_COMPILER})

