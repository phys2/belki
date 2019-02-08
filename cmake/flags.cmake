# overwrite DEBUG, RELEASE build type flags when they are empty/defaults

set(FLAGS_RELEASE_DEFAULT "-O3 -DNDEBUG")
set(FLAGS_DEBUG_DEFAULT "-g")

set(FLAGS_RELEASE -O3 -DNDEBUG -march=nehalem)

set(FLAGS_DEBUG
    # General debug flags:
    -g -Og -fno-omit-frame-pointer
    # -Werror

    # Sanitizers:
    -fsanitize=address,leak,undefined

    # All kinds of pedantic warnings:
    -pedantic -Wall -Wextra
    -Wnull-dereference -Wdouble-promotion -Wformat=2 #-Wshadow

    # Disable warnings that trigger due to known unresolved issues:
    -Wno-sign-compare -Wno-narrowing
    )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # Add GCC-specific flags
	list(APPEND FLAGS_DEBUG
        # Additional warnings that don't come with Wall/Wextra:
        -Wduplicated-cond -Wduplicated-branches -Wlogical-op
		-Wuseless-cast
		)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # Add clang-specific flags
	list(APPEND FLAGS_DEBUG
        # Additional warnings that don't come with Wall/Wextra:
	#  -Wshadow-all
        )
endif()

# Note: it is discouraged in CMake to alter these variables, but instead you
# should set flags on the target. However, those flags are hidden from the
# user. In our case, we want the user to be able to see and alter the
# flags of the build type.
set(TYPES CXX;EXE_LINKER)
set(BUILDS DEBUG;RELEASE)
foreach (BUILD ${BUILDS})
	string(REPLACE ";" " " FLAGS_${BUILD} "${FLAGS_${BUILD}}")
	foreach (TYPE ${TYPES})
		set(VAR "CMAKE_${TYPE}_FLAGS_${BUILD}")

		# Note: we need to apply FORCE for our flags to override the default,
		# but we don't want to override user changes. So we only override if
		# empty or default ("-g")
		string(COMPARE EQUAL "${${VAR}}" "${FLAGS_${BUILD}_DEFAULT}" ISDEFAULT)
		if (NOT ${VAR} OR "${ISDEFAULT}")
			set(${VAR} "${FLAGS_${BUILD}}" CACHE STRING
				"Flags used during ${BUILD} builds." FORCE)
		endif()
	endforeach()
endforeach()

