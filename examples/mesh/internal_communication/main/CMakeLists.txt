idf_component_register(SRCS "mesh_light.c"
                            "mesh_main.cpp"
                            "command.cpp"
                    INCLUDE_DIRS "." "include")

add_compile_options(-Wmissing-field-initializers)
