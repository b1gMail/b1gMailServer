find_path(PCRE_INCLUDE_DIRS pcre.h)

find_library(PCRE_LIBRARIES NAMES libpcre pcre)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PCRE DEFAULT_MSG PCRE_LIBRARIES PCRE_INCLUDE_DIRS)
mark_as_advanced(PCRE_INCLUDE_DIRS PCRE_LIBRARIES)
