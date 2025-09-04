# CopyIfExists.cmake
# Usage:
#   cmake -Dsrc=C:/path/to/src.dll -Ddst=C:/path/to/dest.dll -P CopyFileIfExists.cmake
#
# Copies src -> dst only if src exists. Prints a status line either way.

if (NOT DEFINED src)
  message(FATAL_ERROR "CopyIfExists.cmake: variable 'src' not set")
endif()
if (NOT DEFINED dst)
  message(FATAL_ERROR "CopyIfExists.cmake: variable 'dst' not set")
endif()

if (EXISTS "${src}")
  get_filename_component(_dst_dir "${dst}" DIRECTORY)
  if (_dst_dir)
    file(MAKE_DIRECTORY "${_dst_dir}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
    RESULT_VARIABLE _rc
  )
  if (_rc EQUAL 0)
    message(STATUS "Copied: ${src} -> ${dst}")
  else()
    message(WARNING "CopyIfExists: copy failed (${_rc}): ${src} -> ${dst}")
  endif()
else()
  message(STATUS "Skip copy (not found): ${src}")
endif()
