# obv_core — GUI-free board parse / domain library

# python is required for GenCAD grammar generation (same as desktop)
if(CMAKE_VERSION VERSION_GREATER 3.12)
	if(CMAKE_ANDROID_NDK)
		message(STATUS "obv_core: Using Android NDK Python interpreter")
		set(Python_EXECUTABLE ${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/python3/python)
		find_package(Python REQUIRED COMPONENTS Interpreter)
	else()
		find_package(Python REQUIRED COMPONENTS Interpreter)
	endif()
else()
	find_package(PythonInterp REQUIRED)
	set(Python_EXECUTABLE "${PYTHON_EXECUTABLE}")
endif()

# Prefer iguana already fetched by openboardview; fetch once if building core alone.
include(FetchContent)
if(NOT DEFINED iguana_SOURCE_DIR)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
	FetchContent_Declare(
		iguana
		GIT_REPOSITORY https://github.com/fesily/iguana.git
		GIT_TAG 58dacce85385244e7ab8e47a9912f11c8e74249c
	)
	FetchContent_MakeAvailable(iguana)
	unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

set(OBV_OPENBOARDVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../openboardview")

set(GENCAD_FILE_GRAMMAR_GENERATOR
	"${CMAKE_CURRENT_SOURCE_DIR}/../../utilities/generate_grammar_header.py")
set(GENCAD_FILE_BNF_H
	"${OBV_OPENBOARDVIEW_DIR}/FileFormats/GenCADFileBnf.h")
set(GENERATED_GENCAD_FILE_GRAMMAR_H
	"${CMAKE_CURRENT_BINARY_DIR}/build-generated/GenCADFileGrammar.h")

add_custom_command(OUTPUT "${GENERATED_GENCAD_FILE_GRAMMAR_H}"
	COMMAND "${Python_EXECUTABLE}" "${GENCAD_FILE_GRAMMAR_GENERATOR}" "${GENCAD_FILE_BNF_H}" "${GENERATED_GENCAD_FILE_GRAMMAR_H}"
	DEPENDS "${GENCAD_FILE_GRAMMAR_GENERATOR}" "${GENCAD_FILE_BNF_H}"
	COMMENT "Generating GenCAD grammar header for obv_core"
)

set(OBV_CORE_SOURCES
	src/core_utils.cpp
	src/parse.cpp
	src/board_json.cpp
	src/overlay_store.cpp
	${OBV_OPENBOARDVIEW_DIR}/Board.cpp
	${OBV_OPENBOARDVIEW_DIR}/BRDBoard.cpp
	${OBV_OPENBOARDVIEW_DIR}/annotations.cpp
	${OBV_OPENBOARDVIEW_DIR}/utils.cpp
	${OBV_OPENBOARDVIEW_DIR}/Crypto/des.c
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BRDFileBase.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/ADFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/ASCFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BDVFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BRD2File.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BRDFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BVRFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/BVR3File.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/CADFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/CAEFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/CSTFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/FZFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/GenCADFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/XZZPCBFile.cpp
	${OBV_OPENBOARDVIEW_DIR}/FileFormats/XJsonFile.cpp
	${GENERATED_GENCAD_FILE_GRAMMAR_H}
)

add_library(obv_core STATIC ${OBV_CORE_SOURCES})

target_include_directories(obv_core
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
		# BoardSnapshot exposes BRDBoard / BRDFileBase from openboardview tree
		${OBV_OPENBOARDVIEW_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/..
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}
		${UTF8_INCLUDE_DIR}
		${ZLIB_INCLUDE_DIRS}
)

# iguana (header-only) include path from FetchContent
if(DEFINED iguana_SOURCE_DIR)
	target_include_directories(obv_core PRIVATE ${iguana_SOURCE_DIR})
endif()


# MSVC: expose M_PI via <cmath> (SDL previously provided it for desktop)
if(MSVC)
	target_compile_definitions(obv_core PRIVATE _USE_MATH_DEFINES)
endif()
target_compile_features(obv_core PUBLIC cxx_std_20)

if(ENABLE_SQLITE3)
	target_compile_definitions(obv_core PUBLIC HAVE_SQLITE3)
	target_link_libraries(obv_core PUBLIC SQLite::SQLite3)
endif()

target_link_libraries(obv_core
	PUBLIC
		mpc
		${ZLIB_LIBRARIES}
		${FILESYSTEM_LIBRARIES}
)

# Propagate filesystem mode to consumers of public headers (core_utils.h).
if(NOT FILESYSTEM_LIBRARIES STREQUAL "ghc_filesystem")
	target_compile_definitions(obv_core PUBLIC WITH_STD_FILESYSTEM)
endif()

# Windows needs NOMINMAX for windows headers pulled by platform.h
if(WIN32)
	target_compile_definitions(obv_core PRIVATE NOMINMAX)
endif()
