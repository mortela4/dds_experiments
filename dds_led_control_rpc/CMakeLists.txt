cmake_minimum_required(VERSION 3.16)
project(LedControl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories("/usr/local/include/ddscxx")

if(NOT TARGET CycloneDDS-CXX::ddscxx)
  find_package(CycloneDDS-CXX REQUIRED)
endif()

idlcxx_generate(TARGET LedControl FILES idl/LedControl.idl WARNINGS no-implicit-extensibility)

add_executable(led_server server.cpp)
add_executable(led_client client.cpp)

# Link both executables to idl data type library and ddscxx.
target_link_libraries(led_server CycloneDDS-CXX::ddscxx LedControl)
target_link_libraries(led_client CycloneDDS-CXX::ddscxx LedControl)

set_property(TARGET led_server PROPERTY CXX_STANDARD ${cyclonedds_cpp_std_to_use})
set_property(TARGET led_client PROPERTY CXX_STANDARD ${cyclonedds_cpp_std_to_use})
    
