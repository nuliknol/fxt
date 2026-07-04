QT += widgets network
CONFIG += c++17 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = mf_viewer

INCLUDEPATH += include

SOURCES += \
	src/main.cpp \
	src/mainwindow.cpp \
	src/mf_view.cpp \
	src/hmm_client.cpp \
	src/prediction_layer.cpp \
	src/logger.cpp

HEADERS += \
	include/mainwindow.h \
	include/mf_view.h \
	include/candle.h \
	include/layer.h \
	include/hmm_client.h \
	include/prediction_layer.h \
	include/logger.h
