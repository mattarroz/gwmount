cmake_minimum_required(VERSION 3.28)
project(gwmount)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED fuse3)
pkg_check_modules(LIBSAFEC REQUIRED libsafec)
add_definitions(-DHAVE_LIBSAFEC)

add_library(fatfs
        fatfs/source/ff.c
        fatfs/source/ffsystem.c
        fatfs/source/ffunicode.c
)
target_include_directories(fatfs PUBLIC fatfs/source)

add_library(diskflashback
        DiskFlashback/amiga_sectors.cpp
        DiskFlashback/floppybridge_abstract.h
        DiskFlashback/floppybridge_common.h
        DiskFlashback/floppybridge_lib.cpp
        DiskFlashback/floppybridge_lib.h
        DiskFlashback/ibm_sectors.cpp
        DiskFlashback/MountedVolumes.cpp
        DiskFlashback/MountedVolumes.h
        DiskFlashback/mfminterface.cpp
        DiskFlashback/readwrite_floppybridge.cpp
        DiskFlashback/readwrite_floppybridge.h
        DiskFlashback/sectorCache.cpp
        DiskFlashback/sectorCache.h
        DiskFlashback/sectorCommon.h
)
target_include_directories(diskflashback PRIVATE DiskFlashback ${LIBSAFEC_INCLUDE_DIRS} PUBLIC DiskFlashback/include)
target_link_directories(diskflashback PUBLIC ${LIBSAFEC_LIBRARY_DIRS})
target_link_libraries(diskflashback PUBLIC fatfs ${LIBSAFEC_LIBRARIES})

add_library(floppybridge SHARED
        floppybridge/ArduinoFloppyBridge.cpp
        floppybridge/ArduinoInterface.cpp
        floppybridge/CommonBridgeTemplate.cpp
        floppybridge/ftdi.cpp
        floppybridge/pll.cpp
        floppybridge/GreaseWeazleBridge.cpp
        floppybridge/GreaseWeazleInterface.cpp
        floppybridge/RotationExtractor.cpp
        floppybridge/SerialIO.cpp
        floppybridge/SuperCardProBridge.cpp
        floppybridge/SuperCardProInterface.cpp
        floppybridge/FloppyBridge.cpp
)
target_include_directories(floppybridge PRIVATE floppybridge)

configure_file(fusefatfs/config.h.in fusefatfs/config.h @ONLY)
add_executable(gwmount
        fusefatfs/fftable.c
        fusefatfs/fftable.h
        fusefatfs/fusefatfs.c
)
target_link_libraries(gwmount fatfs diskflashback floppybridge ${FUSE_LIBRARIES})
target_link_directories(gwmount PRIVATE ${FUSE_LIBRARY_DIRS})
target_include_directories(gwmount PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fusefatfs)
target_compile_definitions(gwmount PRIVATE -D_FILE_OFFSET_BITS=64)
