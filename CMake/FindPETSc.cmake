# - Try to find PETSc
# Once done this will define
#
#  PETSC_FOUND - system has PETSc
#  PETSC_INCLUDE_PATH - the PETSc include directories
#  PETSC_LIBRARIES - Link these to use PETSc
#  PETSC_COMPILER - Compiler used by PETSc
#  PETSC_DEFINITIONS - Compiler switches required for using PETSc
#  PETSC_MPIEXEC - Executable for running MPI programs
#
# Setting these changes the behavior of the search
#  PETSC_DIR - directory in which PETSc resides
#  PETSC_ARCH - build architecture
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

# If unset, try environment
if (NOT PETSC_DIR)
  set (PETSC_DIR $ENV{PETSC_DIR})
endif (NOT PETSC_DIR)
if (NOT PETSC_ARCH)
  set (PETSC_ARCH $ENV{PETSC_ARCH})
endif (NOT PETSC_ARCH)

# Crude attempt to determine PETSC_DIR, not useful since we can't
# determine PETSC_ARCH
if (NOT PETSC_DIR)
  find_path (PETSC_DIR include/petsc.h
    PATHS /usr /usr/local $ENV{HOME}/petsc)
endif (NOT PETSC_DIR)

# The configuration is current if both PETSC_DIR and PETSC_ARCH are
# equal to their saved values.  On the first pass, these will match if
# nothing is in the environment, this is okay since PETSC_INCLUDE_PATH
# and PETSC_LIBRARIES are unset.
set (PETSC_CONFIG_CURRENT TRUE)
if (NOT "${PETSC_DIR}" STREQUAL "${PETSC_DIR_PRIVATE}")
  set (PETSC_CONFIG_CURRENT FALSE)
endif (NOT "${PETSC_DIR}" STREQUAL "${PETSC_DIR_PRIVATE}")
if (NOT "${PETSC_ARCH}" STREQUAL "${PETSC_ARCH_PRIVATE}")
  set (PETSC_CONFIG_CURRENT FALSE)
endif (NOT "${PETSC_ARCH}" STREQUAL "${PETSC_ARCH_PRIVATE}")

# Determine whether the PETSc layout is old-style (through 2.3.3) or
# new-style (not yet released, petsc-dev)
if (EXISTS ${PETSC_DIR}/${PETSC_ARCH}/include/petscconf.h) # new
  set (PETSC_CONF_BASE ${PETSC_DIR}/conf/base)
elseif (EXISTS ${PETSC_DIR}/bmake/${PETSC_ARCH}/petscconf.h) # old
  set (PETSC_CONF_BASE ${PETSC_DIR}/bmake/common/base)
else (EXISTS ${PETSC_DIR}/bmake/${PETSC_ARCH}/petscconf.h)
  # The layout is not recognized, how can we give a meaningful warning?
endif (EXISTS ${PETSC_DIR}/${PETSC_ARCH}/include/petscconf.h)

if (PETSC_CONFIG_CURRENT AND PETSC_INCLUDE_PATH AND PETSC_LIBRARIES)
  # Do nothing: all variables are in cache
elseif (PETSC_DIR AND PETSC_ARCH AND CMAKE_MAKE_PROGRAM AND PETSC_CONF_BASE)
  set (PETSC_DIR_PRIVATE ${PETSC_DIR} CACHE INTERNAL "Saved value" FORCE)
  set (PETSC_ARCH_PRIVATE ${PETSC_ARCH} CACHE INTERNAL "Saved value" FORCE)

  # Put variables into environment since they are needed to get
  # configuration (petscvariables) in the PETSc makefile
  set (ENV{PETSC_DIR} ${PETSC_DIR})
  set (ENV{PETSC_ARCH} ${PETSC_ARCH})

  # A temporary makefile to probe the PETSc configuration
  set (PETSC_CONFIG_MAKEFILE ${PROJECT_BINARY_DIR}/Makefile.petsc)

  file (WRITE ${PETSC_CONFIG_MAKEFILE}
"## This file was autogenerated by FindPETSc.cmake
# PETSC_DIR  = ${PETSC_DIR}
# PETSC_ARCH = ${PETSC_ARCH}
include ${PETSC_CONF_BASE}
show_clinker :
	-@echo \${CLINKER}
show_lib_line :
	-@echo \${PETSC_LIB}
show_cpp_line :
	-@echo \${PETSC_CCPPFLAGS}
show_flags :
	-@echo \${CCPPFLAGS}
show_cc : 
	-@echo \${PCC}
show_mpiexec :
	-@echo \${MPIEXEC}
")

  exec_program(${CMAKE_MAKE_PROGRAM}
    ARGS -f ${PETSC_CONFIG_MAKEFILE} show_cpp_line
    OUTPUT_VARIABLE PETSC_CPP_LINE
    RETURN_VALUE PETSC_RETURN)

  exec_program(${CMAKE_MAKE_PROGRAM}
    ARGS -f ${PETSC_CONFIG_MAKEFILE} show_lib_line
    OUTPUT_VARIABLE PETSC_LIB_LINE
    RETURN_VALUE PETSC_RETURN)

  exec_program(${CMAKE_MAKE_PROGRAM}
    ARGS -f ${PETSC_CONFIG_MAKEFILE} show_cc
    OUTPUT_VARIABLE PETSC_CC
    RETURN_VALUE PETSC_RETURN)

  exec_program(${CMAKE_MAKE_PROGRAM}
    ARGS -f ${PETSC_CONFIG_MAKEFILE} show_mpiexec
    OUTPUT_VARIABLE PETSC_MPIEXEC
    RETURN_VALUE PETSC_RETURN)

  file (REMOVE ${PETSC_CONFIG_MAKEFILE})

  # Extract include paths from compile command line
  string (REGEX MATCHALL "-I([^\" ]+|\"[^\"]+\")" PETSC_ALL_INCLUDE_PATHS "${PETSC_CPP_LINE}")
  set (PETSC_INCLUDE_PATH_WORK)
  foreach (IPATH ${PETSC_ALL_INCLUDE_PATHS})
    string (REGEX REPLACE "^-I" "" IPATH ${IPATH})
    string (REGEX REPLACE "//" "/" IPATH ${IPATH})
    list (APPEND PETSC_INCLUDE_PATH_WORK ${IPATH})
  endforeach (IPATH)
  list (REMOVE_DUPLICATES PETSC_INCLUDE_PATH_WORK)

  string (REGEX MATCHALL "(-L|-Wl,|-l)([^\" ]+|\"[^\"]+\")" PETSC_ALL_LINK_TOKENS "${PETSC_LIB_LINE}")
  set (PETSC_LINK_PATHS)
  set (PETSC_LINK_FLAGS_WORK)
  set (PETSC_LIBRARIES_FOUND)
  set (PETSC_LIBRARIES_MISSING)
  foreach (TOKEN ${PETSC_ALL_LINK_TOKENS})
    if (TOKEN MATCHES "-L([^\" ]+|\"[^\"]+\")") # If it's a library path, prepend it to the list
      string (REGEX REPLACE "^-L" "" TOKEN ${TOKEN})
      string (REGEX REPLACE "//" "/" TOKEN ${TOKEN})
      list (INSERT PETSC_LINK_PATHS 0 ${TOKEN})
    elseif (TOKEN MATCHES "-Wl,([^\" ]+|\"[^\"]+\")") # If it's a link flag, put it in the flags list
      if (PETSC_LINK_FLAGS_WORK)
	set (PETSC_LINK_FLAGS_WORK "${PETSC_LINK_FLAGS_WORK} ${TOKEN}")
      else (PETSC_LINK_FLAGS_WORK)
	set (PETSC_LINK_FLAGS_WORK ${TOKEN})
      endif (PETSC_LINK_FLAGS_WORK)
    elseif (TOKEN MATCHES "-l([^\" ]+|\"[^\"]+\")") # If it's a library, get the absolute path by searching in PETSC_LINK_PATHS
      string (REGEX REPLACE "^-l" "" TOKEN ${TOKEN})
      set (PETSC_LIB "PETSC_LIB-NOTFOUND" CACHE FILEPATH "Cleared" FORCE)
      find_library (PETSC_LIB ${TOKEN} HINTS ${PETSC_LINK_PATHS})
      if (PETSC_LIB)
	list (APPEND PETSC_LIBRARIES_FOUND ${PETSC_LIB})
      else (PETSC_LIB)
	list (APPEND PETSC_LIBRARIES_MISSING ${PETSC_LIB})
	message (SEND_ERROR "Unable to find PETSc library ${TOKEN}")
      endif (PETSC_LIB)      
    endif (TOKEN MATCHES "-L([^\" ]+|\"[^\"]+\")")
  endforeach (TOKEN)
  set (PETSC_LIB "PETSC_LIB-NOTFOUND" CACHE INTERNAL "Scratch variable for PETSc detection" FORCE)
  # This is okay on my system, but I think it would break easily.
  list (REMOVE_DUPLICATES PETSC_LIBRARIES_FOUND)

  # We do an out-of-source build so __FILE__ will be an absolute path, hence defining __SDIR__ is superfluous
  set (PETSC_DEFINITIONS "-D__SDIR__=\"\"" CACHE STRING "PETSc definitions")

  # Sometimes this can be used to assist FindMPI.cmake
  set (PETSC_MPIEXEC ${PETSC_MPIEXEC} CACHE FILEPATH "Executable for running PETSc MPI programs")
  set (PETSC_INCLUDE_PATH ${PETSC_INCLUDE_PATH_WORK} CACHE STRING "PETSc include path" FORCE)
  set (PETSC_LIBRARIES ${PETSC_LIBRARIES_FOUND} CACHE STRING "PETSc libraries" FORCE)
  set (PETSC_COMPILER ${PETSC_CC} CACHE FILEPATH "PETSc compiler" FORCE)
endif (PETSC_CONFIG_CURRENT AND PETSC_INCLUDE_PATH AND PETSC_LIBRARIES)

set (PETSC_DIR ${PETSC_DIR} CACHE PATH "PETSc Directory")
set (PETSC_ARCH ${PETSC_ARCH} CACHE STRING "PETSc build architecture")

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (PETSc
  "PETSc could not be found.  Be sure to set PETSC_DIR and PETSC_ARCH."
  PETSC_INCLUDE_PATH PETSC_LIBRARIES)

# show the PETSC_INCLUDE_PATH and PETSC_LIBRARIES variables only in the advanced view
mark_as_advanced (PETSC_INCLUDE_PATH PETSC_LIBRARIES PETSC_COMPILER PETSC_DEFINITIONS PETSC_MPIEXEC)
