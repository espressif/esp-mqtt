set(COMPONENT_ADD_INCLUDEDIRS "include")
set(COMPONENT_PRIV_INCLUDEDIRS "lib/include")
set(COMPONENT_SRCDIRS ". lib")

set(COMPONENT_REQUIRES lwip nghttp mbedtls)

register_component()

