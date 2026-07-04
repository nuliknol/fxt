cmake_minimum_required(VERSION 3.16)
project(mt_gui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

add_executable(mt_gui
	include/main_window.h
	include/api_client.h
	include/cluster_chart_widget.h
	include/micro_harvest_widget.h
	src/main.cpp
	src/main_window.cpp
	src/api_client.cpp
	src/cluster_chart_widget.cpp
	src/micro_harvest_widget.cpp
)

target_include_directories(mt_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(mt_gui PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(mt_gui PRIVATE GUI_BUNDLED_SAMPLE_CSV="${CMAKE_CURRENT_SOURCE_DIR}/../trainer/work/synthetic_mt4.csv")
target_link_libraries(mt_gui PRIVATE Qt5::Widgets Qt5::Network)
