# Sets:
#   TVMET_INCLUDE_DIR  = where tvmet/Vector.h can be found
#   CF_HAVE_TVMET      = set to true after finding the library

ADD_TRIAL_INCLUDE_PATH( ${TVMET_HOME}/include )
ADD_TRIAL_INCLUDE_PATH( $ENV{TVMET_HOME}/include )

FIND_PATH(TVMET_INCLUDE_DIR tvmet/Vector.h ${TRIAL_INCLUDE_PATHS} NO_DEFAULT_PATH)
FIND_PATH(TVMET_INCLUDE_DIR tvmet/Vector.h)

IF(TVMET_INCLUDE_DIR)
  SET(CF_HAVE_TVMET 1 CACHE BOOL "Found tvmet library")
ELSE(TVMET_INCLUDE_DIR)
  SET(CF_HAVE_TVMET 0 CACHE BOOL "Not fount tvmet library")
ENDIF(TVMET_INCLUDE_DIR)

MARK_AS_ADVANCED(
  TVMET_INCLUDE_DIR
  CF_HAVE_TVMET
)

LOG ( "CF_HAVE_TVMET: [${CF_HAVE_TVMET}]" )
IF(CF_HAVE_TVMET)
  LOGFILE ( "  TVMET_INCLUDE_DIR: [${TVMET_INCLUDE_DIR}]" )
ENDIF(CF_HAVE_TVMET)