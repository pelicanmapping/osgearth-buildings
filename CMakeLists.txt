cmake_minimum_required(VERSION 2.8.3 FATAL_ERROR)

#
# Set up CMake to use Solution Folders in VS.
#
set_property( GLOBAL PROPERTY USE_FOLDERS               ON )
set_property( GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake Targets" )

project(OSGEARTH_BUILDINGS)

set(OSGEARTH_BUILDINGS_MAJOR_VERSION 0)
set(OSGEARTH_BUILDINGS_MINOR_VERSION 1)
set(OSGEARTH_BUILDINGS_PATCH_VERSION 0)
set(OSGEARTH_BUILDINGS_SOVERSION     0)

SET(OSGEARTH_BUILDINGS_PLUGIN_PREFIX "")

if (CYGWIN)
    SET(OSGEARTH_BUILDINGS_PLUGIN_PREFIX "cygwin_")
endif(CYGWIN)

if(MINGW)
    SET(OSGEARTH_BUILDINGS_PLUGIN_PREFIX "mingw_")
endif(MINGW)


set(OSGEARTH_BUILDINGS_VERSION ${OSGEARTH_MAJOR_VERSION}.${OSGEARTH_MINOR_VERSION}.${OSGEARTH_PATCH_VERSION})

# We want to build SONAMES shared librariess
set(OSGEARTH_BUILDINGS_SONAMES TRUE)

# We have some custom .cmake scripts not in the official distribution.
# Maybe this can be used override existing behavior if needed?
set(CMAKE_MODULE_PATH "${OSGEARTH_BUILDINGS_SOURCE_DIR}/CMakeModules;${CMAKE_MODULE_PATH}")

# Find our dependencies
find_package(OpenGL)
find_package(OSG)
find_package(osgEarth)

# no idea what this is for. -gw
if(UNIX)
    # Not sure what this will do on Cygwin and Msys
    # Also, remember OS X X11 is a user installed option so it may not exist.
    find_package(X11)
    # Some Unicies need explicit linkage to the Math library or the build fails.
    find_package(MATH_LIBRARY m)
endif(UNIX)

# Visual Studio specific stuff.
if(WIN32)
    if(MSVC)
        # Some MSVC-specific compilation flags
        ADD_DEFINITIONS(-D_SCL_SECURE_NO_WARNINGS)
        ADD_DEFINITIONS(-D_CRT_SECURE_NO_DEPRECATE)
    endif(MSVC)
endif(WIN32)

if(WIN32)
    set(CMAKE_DEBUG_POSTFIX          "d"  CACHE STRING "add a postfix, usually d on windows")
    set(CMAKE_RELEASE_POSTFIX        ""   CACHE STRING "add a postfix, usually empty on windows")
    set(CMAKE_RELWITHDEBINFO_POSTFIX ""   CACHE STRING "add a postfix, usually empty on windows")
    set(CMAKE_MINSIZEREL_POSTFIX     ""   CACHE STRING "add a postfix, usually empty on windows")
else(WIN32)
    set(CMAKE_DEBUG_POSTFIX          "d"  CACHE STRING "add a postfix")
    set(CMAKE_RELEASE_POSTFIX        ""   CACHE STRING "add a postfix")
    set(CMAKE_RELWITHDEBINFO_POSTFIX "rd" CACHE STRING "add a postfix")
    set(CMAKE_MINSIZEREL_POSTFIX     "s"  CACHE STRING "add a postfix")
endif(WIN32)

# Make the headers visible to everything
include_directories(
    ${OSGEARTH_BUILDINGS_INCLUDE_DIR}
    ${OSGEARTH_INCLUDE_DIR}
    ${OSG_INCLUDE_DIR}    
)

# Create bin and lib directories if required (no idea -gw)
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
   file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin ${CMAKE_BINARY_DIR}/lib)
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")


# Installation
if(UNIX AND NOT WIN32 AND NOT APPLE)
  if(CMAKE_SIZEOF_VOID_P MATCHES "8")
      SET(LIB_POSTFIX "64" CACHE STRING "suffix for 32/64 dir placement")
      MARK_AS_ADVANCED(LIB_POSTFIX)
  endif(CMAKE_SIZEOF_VOID_P MATCHES "8")
endif(UNIX AND NOT WIN32 AND NOT APPLE)

if(NOT DEFINED LIB_POSTFIX)
    SET(LIB_POSTFIX "")
endif(NOT DEFINED LIB_POSTFIX)

set(OUTPUT_BINDIR ${PROJECT_BINARY_DIR}/bin)
make_directory(${OUTPUT_BINDIR})
set(EXECUTABLE_OUTPUT_PATH ${OUTPUT_BINDIR})

set(OUTPUT_LIBDIR ${PROJECT_BINARY_DIR}/lib)
make_directory(${OUTPUT_LIBDIR})
set(LIBRARY_OUTPUT_PATH ${OUTPUT_LIBDIR})

link_directories( 
    ${OSG_LIBRARIES_DIR}
    ${OSGEARTH_LIBRARIES_DIR}
    ${LINK_DIRECTORIES} ${OUTPUT_LIBDIR} )


# User options

# Expose CMAKE_INCLUDE_PATH and CMAKE_LIBARY_PATH to the GUI so users
# may set these values without needing to manipulate the environment.
#SET(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} CACHE STRING "You may add additional search paths here. Use ; to separate multiple paths.")
#SET(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} CACHE STRING "You may add additional search paths here. Use ; to separate multiple paths.")
#SET(CMAKE_PREFIX_PATH  ${CMAKE_PREFIX_PATH}  CACHE STRING "(EXPERIMENTAL) You may add additional search paths here. Use ; to separate multiple paths.")

# Dynamic vs Static Linking
option(DYNAMIC_OSGEARTH "Set to ON to build OSGEARTH-BUILDINGS for dynamic linking.  Use OFF for static." ON)
if(DYNAMIC_OSGEARTH)
    set(OSGEARTH_USER_DEFINED_DYNAMIC_OR_STATIC "SHARED")
else(DYNAMIC_OSGEARTH)
    set(OSGEARTH_USER_DEFINED_DYNAMIC_OR_STATIC "STATIC")
endif(DYNAMIC_OSGEARTH)


include(OsgEarthMacroUtils)
DETECT_OSG_VERSION()

set(OPENSCENEGRAPH_VERSION ${OPENSCENEGRAPH_MAJOR_VERSION}.${OPENSCENEGRAPH_MINOR_VERSION}.${OPENSCENEGRAPH_PATCH_VERSION})


# finally!
add_subdirectory(src)
