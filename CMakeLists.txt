cmake_minimum_required(VERSION 3.16)

# Target chip — must be set before project.cmake is included
set(IDF_TARGET esp32s3)

# Use the custom partition table from the project root
set(PARTITION_CSV_PATH "${CMAKE_SOURCE_DIR}/partitions.csv")

set(EXTRA_COMPONENT_DIRS "splash_screen" "display")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(ebadge_app)
