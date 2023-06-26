cmake_minimum_required(VERSION 3.22)

# set the project name
project(Hybrid-Fs VERSION 0.1)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(fuse REQUIRED IMPORTED_TARGET fuse3)

# Add glog support
add_subdirectory(third_party/glog)
link_libraries(glog::glog)

add_subdirectory(third_party/gsl)
link_libraries(Microsoft.GSL::GSL)

aux_source_directory(src SOURCES)
include_directories(include)


# add the executable
add_executable(Hybrid-Fs ${SOURCES})
target_link_libraries(Hybrid-Fs PRIVATE PkgConfig::fuse)