idf_component_register(SRCS "mesh_light.c"
                            "mesh_main.cpp"
                            "command.cpp"
                            "uart_commander.cpp"
                            "ZHNetwork.cpp"
                    INCLUDE_DIRS "." "include")

component_compile_options(-Wmissing-field-initializers)

# nvs_create_partition_image(nvs parameters.csv FLASH_IN_PROJECT)