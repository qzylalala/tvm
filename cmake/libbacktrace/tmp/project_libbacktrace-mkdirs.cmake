# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/qzylalala/work_space/tvm/cmake/libs/../../3rdparty/libbacktrace"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace/tmp"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace/src/project_libbacktrace-stamp"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace/src"
  "/home/qzylalala/work_space/tvm/cmake/libbacktrace/src/project_libbacktrace-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/qzylalala/work_space/tvm/cmake/libbacktrace/src/project_libbacktrace-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/qzylalala/work_space/tvm/cmake/libbacktrace/src/project_libbacktrace-stamp${cfgdir}") # cfgdir has leading slash
endif()
