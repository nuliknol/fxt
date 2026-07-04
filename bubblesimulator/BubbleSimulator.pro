QT += widgets

CONFIG += c++17
TEMPLATE = app
TARGET = BubbleSimulator

SOURCES += \
	main.cpp \
	MainWindow.cpp \
	MarketSimulator.cpp \
	MetricsPanelWidget.cpp \
	ParameterPanelWidget.cpp \
	PriceChartWidget.cpp \
	SimulationControlWidget.cpp

HEADERS += \
	MainWindow.h \
	MarketSimulator.h \
	MetricsPanelWidget.h \
	ParameterPanelWidget.h \
	PriceChartWidget.h \
	SimulationControlWidget.h
