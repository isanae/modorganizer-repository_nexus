cmake_minimum_required(VERSION 3.16)

project(repository_nexus)
set(project_type plugin)

if(DEFINED DEPENDENCIES_DIR)
	include(${DEPENDENCIES_DIR}/modorganizer_super/cmake_common/project.cmake)
else()
	include(../cmake_common/project.cmake)
endif()

add_subdirectory(src)
