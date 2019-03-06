cmake_minimum_required(VERSION 3.9...3.13)

project(Belki
	VERSION 1.0
	DESCRIPTION "Interactive Protein Profile Visualization")

# use our cmake dir for include(…) statements
list(INSERT CMAKE_MODULE_PATH 0 "${PROJECT_SOURCE_DIR}/cmake")

# make CMake behave our way
include(behavior)
# get project dependencies
include(dependencies)
# generate variable PROJECT_VERSION_GIT, provide function export_version()
include(gitversion)
# add custom (default) compile/link flags in variable cache
include(flags)

add_executable(belki MACOSX_BUNDLE main.cpp)
set_target_properties(belki PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON)
export_version(main.cpp)

## PLATFORM SUPPORT
if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set_target_properties(belki PROPERTIES LINK_FLAGS_RELEASE "-Wl,-subsystem,windows")
	target_compile_definitions(belki PRIVATE)
endif()
if (${CMAKE_SYSTEM_NAME} MATCHES "Darvin")
	set(MACOSX_BUNDLE_ICON_FILE icon.icns)
	set_source_files_properties(gfx/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	target_sources(belki PRIVATE gfx/icon.icns)
endif()

## SOURCES
# mean shift
target_sources(belki PRIVATE
	meanshift/fams.h meanshift/fams.cpp
	meanshift/io.cpp meanshift/mode_pruning.cpp
	)

# storage / zip files support
target_sources(belki PRIVATE storage/qzip.h storage/miniz.h storage/miniz.c)

# resources
target_sources(belki PRIVATE resources.qrc gfx/breeze-subset.qrc)

# main application
target_sources(belki PRIVATE
	dataset.h dataset.cpp
	dimred.h dimred.cpp
	fileio.h fileio.cpp
	mainwindow.h mainwindow.cpp mainwindow.ui
	profilechart.h profilechart.cpp
	profilewindow.h profilewindow.cpp profilewindow.ui
	storage.h storage.cpp
	utils.h
	viewer.h
	)

# gui components (they add to target 'belki')
add_subdirectory(scatterplot)
add_subdirectory(heatmap)
add_subdirectory(distmat)

## link to modules
target_include_directories(belki SYSTEM PUBLIC ${DEP_INCLUDES})
target_link_libraries(belki PUBLIC ${DEP_LIBRARIES})
