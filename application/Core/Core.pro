# -------------------------------------------------
# Project created by QtCreator 2009-10-05T21:20:26
# -------------------------------------------------
QT -= gui
QT += network
TARGET = Core
CONFIG += link_prl

CONFIG(debug, debug|release) {
   FOLDER = debug
   DEFINES += DEBUG
} else {
   FOLDER = release
}

INCLUDEPATH += . \
    .. \
    ${PROTOBUF}/src

LIBS += -LFileManager/output/$$FOLDER \
    -lFileManager
POST_TARGETDEPS += FileManager/output/$$FOLDER/libFileManager.a

LIBS += -LPeerManager/output/$$FOLDER \
    -lPeerManager
POST_TARGETDEPS += PeerManager/output/$$FOLDER/libPeerManager.a

LIBS += -LUploadManager/output/$$FOLDER \
    -lUploadManager
POST_TARGETDEPS += UploadManager/output/$$FOLDER/libUploadManager.a

LIBS += -LDownloadManager/output/$$FOLDER \
    -lDownloadManager
POST_TARGETDEPS += DownloadManager/output/$$FOLDER/libDownloadManager.a

LIBS += -LNetworkListener/output/$$FOLDER \
    -lNetworkListener
POST_TARGETDEPS += NetworkListener/output/$$FOLDER/libNetworkListener.a

LIBS += -L../Common/LogManager/output/$$FOLDER \
    -lLogManager
POST_TARGETDEPS += ../Common/LogManager/output/$$FOLDER/libLogManager.a

LIBS += -L../Common/output/$$FOLDER \
    -lCommon
POST_TARGETDEPS += ../Common/output/$$FOLDER/libCommon.a

LIBS += -L${PROTOBUF}/src/.libs \
    -lprotobuf

# FIXME : Theses declarations should not be here, all dependencies are read from the prl files of each library (see link_prl):
win32 {
    INCLUDEPATH += "."
    INCLUDEPATH += "$$(QTDIR)\..\mingw\include"
    LIBS += "$$(QTDIR)\..\mingw\lib\libwsock32.a"
}

DESTDIR = output/$$FOLDER
MOC_DIR = .tmp/$$FOLDER
OBJECTS_DIR = .tmp/$$FOLDER
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
SOURCES += main.cpp \
    Core.cpp
HEADERS += Core.h \
    Log.h
