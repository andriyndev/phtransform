cmake_minimum_required(VERSION 3.16)

project(UI)

include_directories(PH_transformation)

add_subdirectory(PH_transformation)

find_package(wxWidgets REQUIRED)
if(wxWidgets_FOUND)
	include_directories(${wxWidgets_INCLUDE_DIRS})
	include(${wxWidgets_USE_FILE})
else()
	message(SEND_ERROR "wxWidgets are not installed")
	return()
endif()

add_executable(PH_transform_UI main.cpp)

target_link_libraries(PH_transform_UI phtransform ${wxWidgets_LIBRARIES})