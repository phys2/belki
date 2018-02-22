# general

CONFIG += c++14
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS_RELEASE = -O3 -march=nehalem

# platform support

win32 {
    CONFIG += static
    QTPLUGIN += qsvgicon # we use an SVG icon set
    RC_ICONS += gfx/icon.ico
}
macx:ICON = gfx/icon.icns

# dependencies

QT += gui charts

QT += svg printsupport # for plot export

# openmp, eigen, arpack for tapkee
QMAKE_CXXFLAGS += -fopenmp
LIBS += -fopenmp
INCLUDEPATH += /usr/include/eigen3 #PKGCONFIG += eigen3 does not work with mingw
PKGCONFIG += arpack

# tapkee
INCLUDEPATH += $$PWD/include

HEADERS += \
    chart.h \
    mainwindow.h \
    dataset.h \
    dimred.h \
    chartview.h \
    fileio.h

SOURCES += \
    chart.cpp \
    main.cpp \
    mainwindow.cpp \
    dataset.cpp \
    dimred.cpp \
    chartview.cpp \
    fileio.cpp

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc \
    gfx/breeze-subset.qrc
