# - Try to find AmgX
# Once done this will define
#
#  AMGXSOLVER_FOUND        - system has PETSc
#  AMGXSOLVER_INCLUDES     - the PETSc include directories
#  AMGXSOLVER_LIBRARIES    - Link these to use PETSc
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

#cmake_policy(VERSION 3.3)

if( NOT PETSC_FOUND )
  return()
endif()

set(AMGX_VALID_COMPONENTS
    C
    CXX)

SET(CUDA_DIR "$ENV{CUDA_DIR}" CACHE PATH "The path to CUDA.")
find_package(CUDA REQUIRED)

if(${CUDA_FOUND})
    SET(CUDA_LIBRARY_DIRS ${CUDA_TOOLKIT_ROOT_DIR}/lib64/)
    MESSAGE("-- Finding CUDA - Success")
endif()    
    
  # search AMGX header
find_path(AMGX_INCLUDE_DIR amgx_c.h amgx_capi.h
  PATHS ${AMGX_ROOT} ${AMGX_DIR}
  PATH_SUFFIXES include include/amgx
  NO_DEFAULT_PATH
  DOC "Include directory of AMGX")

find_library(AMGX_LIBRARY
    NAMES "amgx" "amgxsh"
    PATHS ${AMGX_ROOT}
    PATH_SUFFIXES "lib" "lib" "lib/${CMAKE_LIBRARY_ARCHITECTURE}"
    NO_DEFAULT_PATH)

find_path(AMGX_WRAPPER_INCLUDE_DIR AmgXSolver.hpp
  PATHS ${AMGX_WRAPPER_ROOT} ${AMGX_WRAPPER_DIR}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
  DOC "Include directory of AMGX_WRAPPER")

find_library(AMGX_WRAPPER_LIBRARY
    NAMES "AmgXWrapper"
    PATHS ${AMGX_WRAPPER_ROOT}
    PATH_SUFFIXES "lib" "lib" "lib/${CMAKE_LIBRARY_ARCHITECTURE}"
    NO_DEFAULT_PATH)

message("amg = ${AMGX_LIBRARY}")
message("amg wrapper = ${AMGX_WRAPPER_LIBRARY}")

if( AMGX_WRAPPER_LIBRARY )
  set(AMGXSOLVER_INCLUDE_DIRS ${AMGX_WRAPPER_INCLUDE_DIR} ${AMGX_INCLUDE_DIR})
  set(AMGXSOLVER_LIBRARIES ${AMGX_WRAPPER_LIBRARY} ${AMGX_LIBRARY} ${CUDA_LIBRARY_DIRS})

  set(AMGXSOLVER_FOUND TRUE)
endif()

if(AMGXSOLVER_FOUND)
  #set HAVE_AMGXSOLVER to 1 for config.h
  set(HAVE_AMGXSOLVER 1)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AmgXSolver DEFAULT_MSG AMGXSOLVER_INCLUDE_DIRS AMGXSOLVER_LIBRARIES)
mark_as_advanced(AMGXSOLVER_INCLUDE_DIRS AMGXSOLVER_LIBRARIES)
