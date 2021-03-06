### fetch project dependencies
set(DEP_INCLUDES "") # for target_include_directories
set(DEP_LIBRARIES "") # for target_link_libraries

if (APPLE)
	include_directories("/usr/local/include" "/usr/local/opt/llvm/include")
	link_directories("/usr/local/lib" "/usr/local/opt/llvm/lib")
endif()

## Tapkee (uses Open MP, ARPACK, Eigen)
find_package(OpenMP)
include(FindPkgConfig)
pkg_check_modules(ARPACK REQUIRED arpack)
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

list(APPEND DEP_INCLUDES
	${PROJECT_SOURCE_DIR}/tapkee # for self-distributed Tapkee
	${ARPACK_INCLUDE_DIRS}
)

list(APPEND DEP_LIBRARIES 
	OpenMP::OpenMP_CXX 
	${ARPACK_LDFLAGS} 
	Eigen3::Eigen
)

## Intel TBB
if (STATIC_BUILD)
	# sadly package does not provide static build so we need to get our hands dirty
	list(APPEND DEP_LIBRARIES tbb_static)
else()
	find_package(TBB REQUIRED)
	list(APPEND DEP_LIBRARIES TBB::tbb)
endif()

## OpenCV
if (STATIC_BUILD)
	set(OpenCV_SHARED OFF FORCE)
endif()
find_package(OpenCV REQUIRED core imgproc)
list(APPEND DEP_INCLUDES ${OpenCV_INCLUDE_DIRS})
if (STATIC_BUILD)
	# sucks that the version is encoded in opencv static lib filenames
	list(APPEND DEP_LIBRARIES opencv_imgproc412 opencv_core412)
else()
	list(APPEND DEP_LIBRARIES ${OpenCV_LIBRARIES})
endif()

## Qt
set(QT_MODULES Concurrent Widgets Charts Svg)
if (STATIC_BUILD AND ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# include plugins into static build on windows
	# (we lack support for static on other platforms right now)
	set(QT_PLUGINS SvgIcon ICO WindowsIntegration WindowsVistaStyle)
endif()

# include core explicitely for AUTOMOC etc
# for the version: 5.12 is an LTS and we also use 5.12 functionality (e.g. CBOR)
find_package(Qt5Core CONFIG 5.12 REQUIRED)
if (STATIC_BUILD)
	set(QT_PREFIX StaticQt5)
else()
	set(QT_PREFIX Qt5)
endif()

# find & add to list for target_link_libraries
foreach(module ${QT_MODULES})
	find_package(${QT_PREFIX}${module} CONFIG REQUIRED)
	list(APPEND DEP_LIBRARIES ${QT_PREFIX}::${module})
endforeach()
foreach(plugin ${QT_PLUGINS})
	list(APPEND DEP_LIBRARIES ${QT_PREFIX}::Q${plugin}Plugin)
endforeach()

# test for our patched QtCharts version; do this every time to reflect changes in Qt5Charts_DIR
unset(HAVE_PATCHED_QTCHARTS CACHE)
set(CMAKE_REQUIRED_LIBRARIES ${QT_PREFIX}::Charts)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <QChart>
#include <QLineSeries>

int main() {
  auto f1 = &QtCharts::QChart::insertSeries;
  auto f2 = &QtCharts::QLineSeries::setDynamicPointSize;
}" HAVE_PATCHED_QTCHARTS)
if (NOT HAVE_PATCHED_QTCHARTS)
  message(FATAL_ERROR "A patched QtCharts is needed. Please specify path via Qt5Charts_DIR variable!")
endif()

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc, create ui headers, generate ressources code
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
