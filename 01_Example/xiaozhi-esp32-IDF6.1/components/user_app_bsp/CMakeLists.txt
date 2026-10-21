# PhotoPainter components contain cyclic callbacks; retain their objects for IDF 6 linking.
idf_component_register(WHOLE_ARCHIVE

  SRCS 
  "mode_src/xiaozhi_mode.cpp" 
  "mode_src/Network_mode.cpp"
  "mode_src/Basic_mode.cpp" 
  "mode_src/Mode_Selection.cpp"
  "mode_src/Daily_mode.cpp"
  "user_app.cpp"
  "daily_photo.cpp"
  PRIV_REQUIRES 
  driver        
  nvs_flash
  esp_wifi
  main
  codec_board
  pmicpower
  esp_http_client
  esp_netif
  mbedtls
  REQUIRES
  app_bsp
  port_bsp
  INCLUDE_DIRS "./")
