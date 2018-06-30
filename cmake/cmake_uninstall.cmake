IF(NOT EXISTS "/data/jameshli/radosfs-master/install_manifest.txt")
  MESSAGE(FATAL_ERROR "Cannot find install manifest: /data/jameshli/radosfs-master/install_manifest.txt")
ENDIF(NOT EXISTS "/data/jameshli/radosfs-master/install_manifest.txt")

FILE(READ "/data/jameshli/radosfs-master/install_manifest.txt" files)
STRING(REGEX REPLACE "\n" ";" files "${files}")
FOREACH(file ${files})
  MESSAGE(STATUS "Uninstalling $ENV{DESTDIR}${file}")
  EXEC_PROGRAM(unlink
      ARGS $ENV{DESTDIR}${file}
      OUTPUT_VARIABLE rm_out
      RETURN_VALUE rm_retval
      )
    IF(NOT "${rm_retval}" STREQUAL 0)
      MESSAGE(FATAL_ERROR "Problem when removing $ENV{DESTDIR}${file}")
    ENDIF(NOT "${rm_retval}" STREQUAL 0)
  ENDIF(EXISTS "$ENV{DESTDIR}${file}")
ENDFOREACH(file)
