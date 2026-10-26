# lightfight firmware (ESP-IDF). Build with ../scripts/idf-build.sh; roles are
# chosen by an sdkconfig overlay: sdkconfig.blaster, sdkconfig.hub.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lightfight)
