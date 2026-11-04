idf_component_register(
    SRCS 
        "presence_controller/presence_controller.c"
    INCLUDE_DIRS 
        "presence_controller"
        "."
    REQUIRES 
        hardware
        middle
        third_party
        esp_timer
        esp_wifi
        esp_event
)


