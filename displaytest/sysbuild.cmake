#
# Sysbuild: include FLPR display driver as remote image
#
# Register the displaydriver with the Partition Manager as a separate
# domain (CPUFLPR), matching the pattern used by the IPC service sample.
#

ExternalZephyrProject_Add(
  APPLICATION displaydriver
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../displaydriver
  BOARD nrf54lm20dk/nrf54lm20a/cpuflpr
  BOARD_REVISION ${BOARD_REVISION}
)

# Register with Partition Manager so it knows how to handle this image
set_property(GLOBAL APPEND PROPERTY PM_DOMAINS CPUFLPR)
set_property(GLOBAL APPEND PROPERTY PM_CPUFLPR_IMAGES displaydriver)
set_property(GLOBAL PROPERTY DOMAIN_APP_CPUFLPR displaydriver)
set(CPUFLPR_PM_DOMAIN_DYNAMIC_PARTITION displaydriver CACHE INTERNAL "")

sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} displaydriver)
sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} displaydriver)
