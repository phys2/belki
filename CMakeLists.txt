cmake_minimum_required(VERSION 3.9...3.13)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")

project(Belki
	VERSION 1.9
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

if (APPLE)
	set_target_properties(${APP_NAME} PROPERTIES 
		MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/resources/macos/Info.plist.in"
		MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION_GIT}
		MACOSX_BUNDLE_COPYRIGHT "(C) 2019 Institute of Physiology II, Universität Freiburg, Germany"
		MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
		MACOSX_BUNDLE_EXECUTABLE_NAME ${APP_NAME}
		MACOSX_BUNDLE_GUI_IDENTIFIER "de.uni-freiburg.physiologie.belki"
		MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_NAME} ${PROJECT_VERSION}"
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
		MACOSX_BUNDLE_ICON_FILE "icon.icns"
		)
	set_source_files_properties(resources/icon.icns PROPERTIES 
		MACOSX_PACKAGE_LOCATION Resources
		)
	target_sources(${APP_NAME} PRIVATE resources/icon.icns)
endif()

## SOURCES
# mean shift
target_sources(${APP_NAME} PRIVATE
	meanshift/fams.h meanshift/fams.cpp
	meanshift/io.cpp meanshift/mode_pruning.cpp
	)

# resources
target_sources(${APP_NAME} PRIVATE
	resources/index.qrc
	resources/icons-stock/index.qrc
	resources/icons-custom/index.qrc
	)

# main application
target_sources(${APP_NAME} PRIVATE
	datahub.h datahub.cpp
	dataset.h dataset.cpp
	fileio.h fileio.cpp
	guistate.h guistate.cpp
	model.h
	proteindb.h proteindb.cpp
	utils.h
	viewer.h viewer.cpp
	windowstate.h windowstate.cpp
	)

# components (they add to target 'belki')
add_subdirectory(compute)
add_subdirectory(storage)
add_subdirectory(widgets)
add_subdirectory(profiles)
add_subdirectory(scatterplot)
add_subdirectory(heatmap)
add_subdirectory(distmat)
add_subdirectory(featweights)

export_version(widgets/mainwindow.cpp) # for About box

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
