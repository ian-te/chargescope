TARGET = harbour-chargescope
CONFIG += sailfishapp c++11

isEmpty(VERSION): VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

HEADERS += src/chargemonitor.h src/kernellog.h
SOURCES += src/main.cpp src/chargemonitor.cpp src/kernellog.cpp

DISTFILES += \
    qml/harbour-chargescope.qml \
    qml/pages/*.qml \
    qml/cover/*.qml \
    qml/components/*.qml \
    qml/js/*.js \
    harbour-chargescope.desktop \
    rpm/harbour-chargescope.spec

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172
