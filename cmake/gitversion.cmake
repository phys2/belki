set(PROJECT_VERSION_GIT ${PROJECT_VERSION})
if (EXISTS ${PROJECT_SOURCE_DIR}/.git OR ${PROJECT_SOURCE_DIR}/../.git)
	find_package(Git)
	if (GIT_FOUND)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			OUTPUT_VARIABLE GIT_HASH
			OUTPUT_STRIP_TRAILING_WHITESPACE
			)
		string(TIMESTAMP DATE "%Y-%m-%d")
		set(PROJECT_VERSION_GIT
			"${PROJECT_VERSION} (${GIT_HASH} built on ${DATE})")
	endif()
endif()

# export version string as a define
function (export_version SOURCEFILE)
	string(TIMESTAMP TODAY "%Y%m%d")
	set_property(SOURCE ${SOURCEFILE} APPEND PROPERTY COMPILE_DEFINITIONS
		PROJECT_VERSION=\"${PROJECT_VERSION_GIT}\"
		PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
		PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
		PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
		PROJECT_VERSION_TWEAK=${PROJECT_VERSION_TWEAK}
		PROJECT_DATE="${TODAY}"
		)
endfunction()

