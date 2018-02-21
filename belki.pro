# general

CONFIG += c++14
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS_RELEASE = -O3 -march=nehalem

# platform support

win32 {
    CONFIG += static
    QTPLUGIN += qsvgicon
    RC_ICONS += gfx/icon.ico
}
macx:ICON = gfx/icon.icns

# dependencies

QT += gui charts

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
    chartview.h

SOURCES += \
    chart.cpp \
    main.cpp \
    mainwindow.cpp \
    dataset.cpp \
    dimred.cpp \
    chartview.cpp

FORMS += \
    mainwindow.ui

RESOURCES += \
    resources.qrc \
    gfx/breeze-subset.qrc
