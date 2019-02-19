## get project dependencies

# Open MP, ARPACK, Eigen used by Tapkee
find_package(OpenMP)
include(FindPkgConfig)
pkg_check_modules(ARPACK REQUIRED arpack)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

# Intel TBB
# sadly package does not provide static build so we need to get our hands dirty
if (NOT STATIC_BUILD)
	find_package(TBB REQUIRED)
endif()

# OpenCV
if (STATIC_BUILD)
	set(OpenCV_SHARED OFF FORCE)
endif()
find_package(OpenCV REQUIRED core imgproc)

# Qt
if (STATIC_BUILD)
	set(USE_STATIC_QT_BY_DEFAULT ON)
endif()
find_package(Qt5Widgets CONFIG REQUIRED)
find_package(Qt5Charts CONFIG REQUIRED)
find_package(Qt5Svg CONFIG REQUIRED)

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc, create ui headers, genereate ressources code
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
