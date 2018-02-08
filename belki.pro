# compiler config

CONFIG += c++14
win32 {
    CONFIG += static
    RC_ICONS += gfx/icon.ico
}
macx:ICON = gfx/icon.icns

QMAKE_CXXFLAGS_RELEASE = -O3 -march=nehalem

# dependencies

QT += gui charts

QMAKE_CXXFLAGS += -fopenmp
LIBS += -fopenmp

CONFIG += link_pkgconfig
INCLUDEPATH += /usr/include/eigen3 #PKGCONFIG += eigen3 does not work with mingw
PKGCONFIG += arpack

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
