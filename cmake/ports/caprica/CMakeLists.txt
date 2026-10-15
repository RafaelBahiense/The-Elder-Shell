cmake_minimum_required(VERSION 3.25)
project(Caprica VERSION 0.3.0 LANGUAGES CXX)

find_package(fmt CONFIG REQUIRED)
find_package(boost_container CONFIG REQUIRED)
file(GLOB_RECURSE sources CONFIGURE_DEPENDS Caprica/*.cpp)
list(FILTER sources EXCLUDE REGEX "/main(_options)?\\.cpp$")

add_library(caprica STATIC ${sources})
target_compile_features(caprica PUBLIC cxx_std_23)
target_include_directories(caprica SYSTEM PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Caprica>"
  "$<INSTALL_INTERFACE:include/caprica>")
target_link_libraries(caprica PUBLIC fmt::fmt Boost::container PRIVATE advapi32)
target_compile_definitions(caprica PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_compile_options(caprica PRIVATE /EHsc /utf-8 /W0)

include(GNUInstallDirs)
install(TARGETS caprica EXPORT CapricaTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY Caprica/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caprica
  FILES_MATCHING PATTERN "*.h" PATTERN "cmake" EXCLUDE)
install(EXPORT CapricaTargets NAMESPACE Caprica:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Caprica)

include(CMakePackageConfigHelpers)
string(SHA256 CAPRICA_BUILD_ID "${CAPRICA_BUILD_ID};${fmt_VERSION};${boost_container_VERSION}")
configure_package_config_file(CapricaConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/CapricaConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Caprica)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CapricaConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CapricaConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CapricaConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Caprica)
