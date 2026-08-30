FILE(GLOB_RECURSE main ${CMAKE_SOURCE_DIR}/main/*.c ${CMAKE_SOURCE_DIR}/main/*.cpp)

idf_component_register(SRCS ${main}
                        INCLUDE_DIRS "include" 
                        REQUIRES bsp_illuminate bsp_camera human_face_detect esp-dl esp_tinyusb)
                                 
