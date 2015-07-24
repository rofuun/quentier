message(STATUS "Looking for system wide installation of tidy-html5")
find_path(TIDY_HTML5_INCLUDE_PATH tidy.h SILENT REQUIRED)
if(NOT TIDY_HTML5_INCLUDE_PATH)
  message(FATAL_ERROR "Can't find development header tidy.h")
elseif(NOT EXISTS ${TIDY_HTML5_INCLUDE_PATH}/tidyenum.h)
  message(FATAL_ERROR "Found path to tidy.h in ${TIDY_HTML5_INCLUDE_PATH} but can't find tidyenum.h in the same folder")
elseif(NOT EXISTS ${TIDY_HTML5_INCLUDE_PATH}/tidyplatform.h)
  message(FATAL_ERROR "Found path to tidy.h in ${TIDY_HTML5_INCLUDE_PATH} but can't find tidyplatform.h in the same folder")
else()
  message(STATUS "Found development headers for tidy-html5 in folder ${TIDY_HTML5_INCLUDE_PATH}")
  find_library(TIDY_HTML5_LIB
               NAMES libtidy5.so libtidy5.a libtidy5.dylib libtidy5.dll tidy5.dll libtidy5.lib tidy5.lib
                     libtidy.so libtidy.a libtidy.dylib libtidy.dll tidy.dll libtidy.lib tidy.lib
               SILENT REQUIRED)
  if(NOT TIDY_HTML5_LIB)
    message(FATAL_ERROR "Can't find tidy-html5 library")
  else()
    message(STATUS "Found tidy-html5 library: ${TIDY_HTML5_LIB}")
  endif()
endif()
