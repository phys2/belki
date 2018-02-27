# GENERAL

CONFIG += c++14
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS_RELEASE = -O3 -march=nehalem

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

# tapkee (and our own qtcharts)
INCLUDEPATH += $$PWD/include
#linux:LIBS += $$PWD/lib/qtcharts-linux/libQt5Charts.a
#win32:LIBS += $$PWD/lib/qtcharts-mingw/libQt5Charts.a
#macx:QT += charts # will break sooner or later

HEADERS += \
    chart.h \
    mainwindow.h \
    dataset.h \
    dimred.h \
    chartview.h \
    fileio.h \
    profilewindow.h

SOURCES += \
    chart.cpp \
    main.cpp \
    mainwindow.cpp \
    dataset.cpp \
    dimred.cpp \
    chartview.cpp \
    fileio.cpp \
    profilewindow.cpp

FORMS += \
    mainwindow.ui \
    profilewindow.ui

RESOURCES += \
    resources.qrc \
    gfx/breeze-subset.qrc
