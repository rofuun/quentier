message("Looking for system-wide installation of SimpleCrypt")
find_path(SIMPLECRYPT_INCLUDE_PATH simplecrypt.h SILENT REQUIRED)
if(NOT SIMPLECRYPT_INCLUDE_PATH)
  message(FATAL_ERROR "Can't find development header simplecrypt.h")
else()
  message(STATUS "Found development headers for SimpleCrypt in folder ${SIMPLECRYPT_INCLUDE_PATH}")
  find_library(SIMPLECRYPT_LIB 
               NAMES libsimplecrypt.so libsimplecrypt.a libsimplecrypt.dylib libsimplecrypt.dll libsimplecrypt.lib simplecrypt.dll simplecrypt.lib
               SILENT REQUIRED)
  if(NOT SIMPLECRYPT_LIB)
    message(FATAL_ERROR "Can't find SimpleCrypt library")
  else()
    message(STATUS "Found SimpleCrypt library: ${SIMPLECRYPT_LIB}")
  endif()
endif()
