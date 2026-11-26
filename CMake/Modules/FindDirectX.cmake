# From the CMake wiki, get the DirectX version needed.
# This assumes default directories.

# Once loaded, the following are defined:
#  DIRECTX_FOUND
#  DIRECTX_INCLUDE_DIR
#  DIRECTX_LIBRARIES

# Direct3D 9, DirectSound and DirectInput headers/libs ship with the modern
# Windows SDK. The legacy June 2010 DirectX SDK (DXSDK_DIR) is no longer
# needed: the engine stopped using D3DX and DxErr. If it is installed anyway,
# its paths are still picked up, but nothing requires it.

if(NOT WIN32)
  return()
endif()

if(NOT EXISTS "$ENV{DXSDK_DIR}")
  set(DIRECTX_FOUND TRUE)
  set(DIRECTX_INCLUDE_DIR "")
  set(DIRECTX_LIBRARIES "")
  return()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(DIRECTX_ARCH "x64")
else()
  set(DIRECTX_ARCH "x86")
endif()

set(DIRECTX_INCLUDE_SEARCH_PATHS
  "$ENV{DXSDK_DIR}/Include"
)

set(DIRECTX_LIBRARY_SEARCH_PATHS
  "$ENV{DXSDK_DIR}/Lib/${DIRECTX_ARCH}"
)

find_path(DIRECTX_INCLUDE_DIR
  NAMES "DxErr.h"
  PATHS ${DIRECTX_INCLUDE_SEARCH_PATHS}
  DOC "Where can DxErr.h be found?"
)

find_library(DIRECTX_LIBRARIES
  NAMES "DxErr.lib" "d3dx9.lib"
  PATHS ${DIRECTX_LIBRARY_SEARCH_PATHS}
  DOC "Where can the DX libraries be found?"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DIRECTX DEFAULT_MSG DIRECTX_INCLUDE_DIR DIRECTX_LIBRARIES)

if(DIRECTX_FOUND)
  mark_as_advanced(DIRECTX_INCLUDE_DIR DIRECTX_LIBRARIES)
endif()
