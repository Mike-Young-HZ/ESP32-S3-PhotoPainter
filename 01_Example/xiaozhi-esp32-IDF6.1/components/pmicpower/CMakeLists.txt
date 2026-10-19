set(src_dirs  ./
              ./src)
set(include_dirs  ./ 
                  ./src
                  ./src/REG)
# PhotoPainter components contain cyclic callbacks; retain their objects for IDF 6 linking.
idf_component_register(WHOLE_ARCHIVE

  SRC_DIRS ${src_dirs}
  INCLUDE_DIRS ${include_dirs}
  REQUIRES port_bsp driver
)
add_compile_definitions(XPOWERS_CHIP_AXP2101 CONFIG_XPOWERS_ESP_IDF_NEW_API)
##REQUIRES