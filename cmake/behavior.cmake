# old versions don't support our min.required string, manually up the policy
if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

# write all executables to root of build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# set default build type to RELEASE
# from cmake devs, https://blog.kitware.com/cmake-and-the-default-build-type/
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# STATIC stuff (Windows)
if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(BUILD_FOR_WIN TRUE)
endif()
option(STATIC_BUILD "Try to build a static binary. Good luck!" ${BUILD_FOR_WIN})

if (STATIC_BUILD)
	set(CMAKE_EXE_LINKER_FLAGS "-static")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" CONFIG)
	set(BUILD_SHARED_LIBS OFF)
endif()

# EXPERIMENTAL features
option(EXPERIMENTAL "Enable experimental features" FALSE)
function (export_experimental SOURCEFILE)
	if (EXPERIMENTAL)
		set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCEFILE} APPEND PROPERTY COMPILE_DEFINITIONS
			EXPERIMENTAL
			)
	endif()
endfunction()
