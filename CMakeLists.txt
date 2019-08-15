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

set(APP_NAME belki)

add_executable(${APP_NAME} MACOSX_BUNDLE main.cpp)
set_target_properties(${APP_NAME} PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON)
export_version(main.cpp)

## PLATFORM SUPPORT
if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set_target_properties(${APP_NAME} PROPERTIES LINK_FLAGS_RELEASE "-Wl,-subsystem,windows")
	target_sources(${APP_NAME} PRIVATE resources/windows.rc)
	export_version(resources/windows.rc)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(MACOSX_BUNDLE_INFO_PLIST "resources/macos/Info.plist")
	set(MACOSX_BUNDLE_ICON_FILE icon.icns)
	set_source_files_properties(resources/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
	target_sources(${APP_NAME} PRIVATE resources/icon.icns)
endif()

## SOURCES
# mean shift
target_sources(${APP_NAME} PRIVATE
	meanshift/fams.h meanshift/fams.cpp
	meanshift/io.cpp meanshift/mode_pruning.cpp
	)

# storage / zip files support
target_sources(${APP_NAME} PRIVATE storage/qzip.h storage/miniz.h storage/miniz.c)

# resources
target_sources(${APP_NAME} PRIVATE
	resources/index.qrc
	resources/icons-stock/index.qrc
	resources/icons-custom/index.qrc
	)

# main application
target_sources(${APP_NAME} PRIVATE
	colormap.h colormap.cpp
	dataset.h dataset.cpp
	dimred.h dimred.cpp
	fileio.h fileio.cpp
	mainwindow.h mainwindow.cpp mainwindow.ui
	storage.h storage.cpp
	utils.h
	viewer.h
	)

# gui components (they add to target 'belki')
add_subdirectory(widgets)
add_subdirectory(scatterplot)
add_subdirectory(heatmap)
add_subdirectory(distmat)
add_subdirectory(featweights)

## link to modules
target_include_directories(${APP_NAME} SYSTEM PUBLIC ${DEP_INCLUDES})
target_link_libraries(${APP_NAME} PUBLIC ${DEP_LIBRARIES})

### Destination paths below are relative to ${CMAKE_INSTALL_PREFIX}
##install(TARGETS ${APP_NAME}
##    BUNDLE DESTINATION . COMPONENT Runtime
##    RUNTIME DESTINATION bin COMPONENT Runtime
##    )
##
### Note Mac specific extension .app
##set(APPS "\${CMAKE_INSTALL_PREFIX}/${APP_NAME}.app")
##
### Directories to look for dependencies
##set(DIRS ${CMAKE_BINARY_DIR})
##
##install(CODE "include(BundleUtilities)
##    fixup_bundle(\"${APPS}\" \"\" \"${DIRS}\")")
##
##set(CPACK_GENERATOR "DRAGNDROP")
##include(CPack)
