# obv_server - thin HTTP front-end over obv_core (cpp-httplib)

if(NOT TARGET obv_core)
	message(FATAL_ERROR "obv_server requires ENABLE_OBV_CORE and target obv_core")
endif()

add_executable(obv_server
	main.cpp
	server_config.cpp
)

target_include_directories(obv_server
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_features(obv_server PRIVATE cxx_std_20)

target_link_libraries(obv_server
	PRIVATE
		obv_core
)

# cpp-httplib on Windows needs Winsock + related system libs
if(WIN32)
	target_link_libraries(obv_server PRIVATE ws2_32)
	# NOMINMAX already useful if any Windows headers leak in
	target_compile_definitions(obv_server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# MSVC: avoid min/max macros; enable /utf-8 if project later adds non-ASCII
if(MSVC)
	target_compile_options(obv_server PRIVATE /W3)
endif()

install(TARGETS obv_server RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR})
