vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO Orvid/Caprica
  REF 2042c902ec269e33c1061ccd8aac0760c981253b
  SHA512 907b7d934f67ec0b31e4f5fe9d14e759c0aa564b82da7dc56226f2702ce0a601428f040718403bc4b3fc121c527b7a83026fd0b675b898d9d84254386398c90c
  PATCHES patches/sessions.patch patches/fmt.patch patches/pooled-initializers.patch
)

# The CLI-oriented upstream build is replaced by a library-only build.
file(COPY "${CURRENT_PORT_DIR}/CMakeLists.txt" "${CURRENT_PORT_DIR}/CapricaConfig.cmake.in"
  DESTINATION "${SOURCE_PATH}")

# vcpkg tracks the port for binary caching; expose the same source identity to
# the console's PEX cache, so changed patches cannot reuse older bytecode.
file(GLOB_RECURSE port_files LIST_DIRECTORIES false "${CURRENT_PORT_DIR}/*")
set(build_identity "2042c902ec269e33c1061ccd8aac0760c981253b")
foreach(file IN LISTS port_files)
  file(SHA256 "${file}" digest)
  string(APPEND build_identity ";${digest}")
endforeach()
string(SHA256 build_identity "${build_identity}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS "-DCAPRICA_BUILD_ID=${build_identity}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Caprica CONFIG_PATH lib/cmake/Caprica)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
