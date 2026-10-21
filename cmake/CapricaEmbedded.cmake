find_package(Caprica CONFIG REQUIRED)
add_library(caprica_embedded INTERFACE)
target_link_libraries(caprica_embedded INTERFACE Caprica::caprica)

# Adapter changes also invalidate PEX caches, independently of the library port.
set(profile_files
  "${PROJECT_SOURCE_DIR}/src/native/compiler/EmbeddedCompiler.cpp"
  "${PROJECT_SOURCE_DIR}/src/native/compiler/EmbeddedCompiler.h"
  "${PROJECT_SOURCE_DIR}/src/native/compiler/Compilation.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${profile_files})
set(profile "${Caprica_BUILD_ID}")
foreach(file IN LISTS profile_files)
  file(SHA256 "${file}" digest)
  string(APPEND profile ";${digest}")
endforeach()
string(SHA256 profile "${profile}")
target_compile_definitions(caprica_embedded INTERFACE EMBEDDED_CAPRICA_PROFILE="${profile}")
