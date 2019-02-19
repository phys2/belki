# GENERAL

CONFIG += c++1z
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS_RELEASE = -O3 -march=nehalem

QMAKE_CXXFLAGS_DEBUG += -fsanitize=address,leak,undefined
debug:QMAKE_LIBS += -fsanitize=address,leak,undefined

# PLATFORM SUPPORT

win32 {
    CONFIG += static
    #QTPLUGIN += qsvgicon # if not building with SVG, used for svg icons
    RC_ICONS += gfx/icon.ico
}
macx:ICON = gfx/icon.icns

# DEPENDENCIES

QT += gui widgets
QT += charts # (disable if using own qtcharts)
QT += svg # for plot export

# openmp, eigen, arpack for tapkee
QMAKE_CXXFLAGS += -fopenmp
LIBS += -fopenmp
INCLUDEPATH += /usr/include/eigen3 #PKGCONFIG += eigen3 does not work with mingw
PKGCONFIG += arpack
# opencv for distance computations
PKGCONFIG += opencv4
# TBB for micro-parallelization
win32 {
    LIBS += -ltbb_static
} else {
    CONFIG(release, debug|release):LIBS += -ltbb
    CONFIG(debug, release|debug):LIBS += -ltbb_debug
}

# tapkee (and our own qtcharts)
INCLUDEPATH += $$PWD/include
#linux:LIBS += $$PWD/lib/qtcharts-linux/libQt5Charts.a
#win32:LIBS += $$PWD/lib/qtcharts-mingw/libQt5Charts.a
#macx:QT += charts # will break sooner or later

# FILES

HEADERS += \
    meanshift/fams.h \
    storage/qzip.h \
    storage/miniz.h \
    chart.h \
    mainwindow.h \
    dataset.h \
    dimred.h \
    chartview.h \
    fileio.h \
    profilewindow.h \
    profilechart.h \
    storage.h \
    heatmap/heatmaptab.h \
    heatmap/heatmapscene.h \
    heatmap/heatmapview.h \
    distmat/distmattab.h \
    distmat/distmatscene.h \
    distmat/distmatview.h \
    distmat/distmat.h \
    utils.h \
    viewer.h

SOURCES += \
    meanshift/fams.cpp \
    meanshift/io.cpp \
    meanshift/mode_pruning.cpp \
    storage/miniz.c \
    chart.cpp \
    main.cpp \
    mainwindow.cpp \
    dataset.cpp \
    dimred.cpp \
    chartview.cpp \
    fileio.cpp \
    profilewindow.cpp \
    profilechart.cpp \
    storage.cpp \
    heatmap/heatmaptab.cpp \
    heatmap/heatmapscene.cpp \
    heatmap/heatmapview.cpp \
    distmat/distmattab.cpp \
    distmat/distmatscene.cpp \
    distmat/distmatview.cpp \
    distmat/distmat.cpp

FORMS += \
    mainwindow.ui \
    profilewindow.ui \
    heatmap/heatmaptab.ui \
    distmat/distmattab.ui

RESOURCES += \
    resources.qrc \
    gfx/breeze-subset.qrc
