# - Try to find libevent
# FindLibevent
# ------------
#
# Find Libevent include directories and libraries. Invoke as::
#
# find_package(Libevent)
#
# Valid components are one or more of:: libevent core extra pthreads openssl.
# Note that 'libevent' contains both core and extra. You must specify one of
# them for the other components.
#
# This module will define the following variables::
#
# LIBEVENT_FOUND        - True if headers and requested libraries were found
# LIBEVENT_INCLUDE_DIRS - Libevent include directories
# LIBEVENT_LIBRARIES    - Libevent libraries to be linked

find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBEVENT QUIET libevent)

# Look for the Libevent 2.x
find_path(LIBEVENT_INCLUDE_DIR NAMES event2/event-config.h HINTS ${PC_LIBEVENT_INCLUDE_DIR})

find_library(LIBEVENT_LIBRARY NAMES event)
find_library(LIBEVENT_CORE NAMES event_core)
find_library(LIBEVENT_EXTRA NAMES event_extra)
find_library(LIBEVENT_THREAD NAMES event_pthreads)
find_library(LIBEVENT_SSL NAMES event_openssl)

set(LIBEVENT_INCLUDE_DIR ${LIBEVENT_INCLUDE_DIR})
set(LIBEVENT_LIBRARIES
  ${LIBEVENT_LIBRARY}
  ${LIBEVENT_CORE}
  ${LIBEVENT_EXTRA}
  ${LIBEVENT_THREAD}
  ${LIBEVENT_SSL})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libevent DEFAULT_MSG LIBEVENT_LIBRARIES LIBEVENT_INCLUDE_DIR)
mark_as_advanced(LIBEVENT_INCLUDE_DIR LIBEVENT_LIBRARIES)

if(LIBEVENT_FOUND)
  add_library(libevent::core UNKNOWN IMPORTED)
  set_target_properties(libevent::core PROPERTIES
    IMPORTED_LOCATION event_core
    INTERFACE_INCLUDE_DIRECTORIES ${LIBEVENT_INCLUDE_DIR}
  )
  add_library(libevent::extra UNKNOWN IMPORTED)
  set_target_properties(libevent::extra PROPERTIES
    IMPORTED_LOCATION event_extra
    INTERFACE_INCLUDE_DIRECTORIES ${LIBEVENT_INCLUDE_DIR}
  )
endif()
