# Desktop Qt 5 is enough: the tests read copies of phones' sysfs files from devices/.
QT = core testlib
CONFIG += c++11 console testcase
CONFIG -= app_bundle
TARGET = tst_chargescope

DEFINES += APP_VERSION=\\\"test\\\" FIXTURES=\\\"$$PWD/devices\\\"
INCLUDEPATH += ../src
HEADERS += ../src/chargemonitor.h ../src/kernellog.h
SOURCES += tst_chargescope.cpp ../src/chargemonitor.cpp ../src/kernellog.cpp
