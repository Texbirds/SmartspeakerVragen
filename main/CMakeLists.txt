set(COMPONENT_SRCS 
    "main.c"
    "button.c"
    "recorder.c"
    "sdcard.c"
    "wifi.c"
    "transcriber.c"
    "chatgpt_api.c"
    "lcd.c"
)

set(COMPONENT_ADD_INCLUDEDIRS .)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
    EMBED_TXTFILES "server_cert.pem" 
)
