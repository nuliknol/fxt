cmake_minimum_required(VERSION 3.16)
project(mt_trainer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MT_ENABLE_HIP_BACKEND "Enable HIP distance backend" ON)

find_package(Threads REQUIRED)

set(TRAINER_CORE_SOURCES
	src/dataset_store.c
	src/mt4_csv_reader.c
	src/candle_encode.c
	src/char_block.c
	src/char_distance.c
	src/gpu_dispatch.c
	src/cluster_graph.c
	src/cluster_kmedoids.c
	src/cluster_dbscan.c
	src/symbol_stream.c
	src/rle.c
	src/word_mining.c
	src/phrase_mining.c
	src/wave_dp.c
	src/wave_predictor.c
	src/live_store.c
	src/micro_harvest.c
	src/logger.c
	src/job_manager.c
	src/json_codec.c
	src/http_api.c
)

add_library(trainer_core ${TRAINER_CORE_SOURCES})

target_include_directories(trainer_core
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(trainer_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(trainer_core PUBLIC Threads::Threads m)

if(MT_ENABLE_HIP_BACKEND)
	find_program(HIPCC_BIN hipcc)
	if(HIPCC_BIN)
		set(HIP_RUNTIME_LIB "")
		set(STDCPP_LIB "")
		if(EXISTS "/opt/rocm/lib/libamdhip64.so")
			set(HIP_RUNTIME_LIB "/opt/rocm/lib/libamdhip64.so")
		elseif(EXISTS "/opt/rocm-6.2.4/lib/libamdhip64.so")
			set(HIP_RUNTIME_LIB "/opt/rocm-6.2.4/lib/libamdhip64.so")
		endif()
		if(EXISTS "/lib/x86_64-linux-gnu/libstdc++.so.6")
			set(STDCPP_LIB "/lib/x86_64-linux-gnu/libstdc++.so.6")
		elseif(EXISTS "/usr/lib/x86_64-linux-gnu/libstdc++.so.6")
			set(STDCPP_LIB "/usr/lib/x86_64-linux-gnu/libstdc++.so.6")
		endif()
		if(NOT HIP_RUNTIME_LIB OR NOT STDCPP_LIB)
			message(WARNING "MT_ENABLE_HIP_BACKEND=ON but HIP runtime link libraries not found; using CPU fallback")
		else()
		add_custom_command(
			OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hip_distance_kernels.o
			COMMAND ${HIPCC_BIN} -fPIC -c ${CMAKE_CURRENT_SOURCE_DIR}/src/hip_distance_kernels.hip.cpp -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${CMAKE_CURRENT_BINARY_DIR}/hip_distance_kernels.o
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hip_distance_kernels.hip.cpp
			VERBATIM
		)
		add_custom_command(
			OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hip_wave_kernels.o
			COMMAND ${HIPCC_BIN} -fPIC -c ${CMAKE_CURRENT_SOURCE_DIR}/src/hip_wave_kernels.hip.cpp -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${CMAKE_CURRENT_BINARY_DIR}/hip_wave_kernels.o
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/hip_wave_kernels.hip.cpp
			VERBATIM
		)
		add_custom_target(hip_obj ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/hip_distance_kernels.o ${CMAKE_CURRENT_BINARY_DIR}/hip_wave_kernels.o)
		add_library(trainer_hip_obj OBJECT IMPORTED GLOBAL)
		set_property(TARGET trainer_hip_obj PROPERTY IMPORTED_OBJECTS ${CMAKE_CURRENT_BINARY_DIR}/hip_distance_kernels.o;${CMAKE_CURRENT_BINARY_DIR}/hip_wave_kernels.o)
		target_sources(trainer_core PRIVATE $<TARGET_OBJECTS:trainer_hip_obj>)
		target_compile_definitions(trainer_core PRIVATE TRAINER_ENABLE_HIP_BACKEND=1)
		target_link_libraries(trainer_core PUBLIC ${HIP_RUNTIME_LIB} ${STDCPP_LIB})
		endif()
	else()
		message(WARNING "MT_ENABLE_HIP_BACKEND=ON but hipcc not found; using CPU fallback")
	endif()
endif()

add_executable(trainer_daemon src/server_main.c)
target_link_libraries(trainer_daemon PRIVATE trainer_core)

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE trainer_core)
target_compile_definitions(test_core PRIVATE TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(test_live_store tests/test_live_store.c)
target_link_libraries(test_live_store PRIVATE trainer_core)

add_executable(test_micro_harvest tests/test_micro_harvest.c)
target_link_libraries(test_micro_harvest PRIVATE trainer_core)

enable_testing()
add_test(NAME core_tests COMMAND test_core)
add_test(NAME live_store_tests COMMAND test_live_store)
add_test(NAME micro_harvest_tests COMMAND test_micro_harvest)
