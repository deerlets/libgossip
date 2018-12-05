cmake_minimum_required(VERSION 2.8)

file(GLOB SRC *.c lib/*.c)
file(GLOB INC *.h)

add_library(gossip SHARED ${SRC})
target_link_libraries(gossip json-c crypto)
set_target_properties(gossip PROPERTIES VERSION 0.1.0 SOVERSION 0.1)

if (WIN32)
target_link_libraries(gossip ws2_32)
endif ()

install(TARGETS gossip
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib)

install(FILES ${INC} DESTINATION include/gossip)

add_subdirectory(client)
