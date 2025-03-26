idf_component_register(SRCS "us100.c"
    REQUIRES driver
    PRIV_REQUIRES esp32-uart-us-100
    INCLUDE_DIRS ".")
