cmake_minimum_required(VERSION 3.16)

project(PH_transformation)

find_package(OpenCL REQUIRED)
if(OpenCL_FOUND)
	include_directories(${OpenCL_INCLUDE_DIRS})
else()
	message(SEND_ERROR "OpenCL is not installed")
	return()
endif()

add_library(phtransform STATIC main.c)

target_link_libraries(phtransform ${OpenCL_LIBRARIES})

#configure_file(kernel.cl ${RUNTIME_OUTPUT_DIRECTORY}/kernel.cl COPYONLY)