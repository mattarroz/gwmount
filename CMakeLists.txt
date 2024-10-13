cmake_minimum_required(VERSION 3.28)
project(gwmount)

set(CMAKE_CXX_STANDARD 20)

add_library(fatfs
        fatfs/source/ff.c
        fatfs/source/ffsystem.c
        fatfs/source/ffunicode.c
)
target_include_directories(fatfs PUBLIC fatfs/source)
target_compile_definitions(fatfs PUBLIC )


find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED fuse3)
pkg_check_modules(LIBSAFEC REQUIRED libsafec)
add_definitions(-DHAVE_LIBSAFEC)
add_library(diskflashback
#        DiskFlashback/adf_nativedriver.cpp
#        DiskFlashback/adf_nativedriver.h
#        DiskFlashback/adf_operations.cpp
#        DiskFlashback/adf_operations.h
#        DiskFlashback/amiga_operations.cpp
#        DiskFlashback/amiga_operations.h
#        DiskFlashback/amiga_sectors.cpp
#        DiskFlashback/amiga_sectors.h
#        DiskFlashback/drivecontrol.h
#        DiskFlashback/DriveList.cpp
#        DiskFlashback/DriveList.h
#        DiskFlashback/fat_operations.cpp
#        DiskFlashback/fat_operations.h
        DiskFlashback/floppybridge_abstract.h
        DiskFlashback/floppybridge_common.h
        DiskFlashback/floppybridge_lib.cpp
        DiskFlashback/floppybridge_lib.h
#        DiskFlashback/ibm_sectors.cpp
#        DiskFlashback/ibm_sectors.h
#        DiskFlashback/mfminterface.cpp
#        DiskFlashback/mfminterface.h
#        DiskFlashback/MountedVolume.cpp
#        DiskFlashback/MountedVolume.h
        DiskFlashback/MountedVolumes.cpp
        DiskFlashback/MountedVolumes.h
#        DiskFlashback/pfs3_operations.cpp
#        DiskFlashback/pfs3_operations.h
#        DiskFlashback/pll.cpp
#        DiskFlashback/pll.h
#        DiskFlashback/readwrite_dms.cpp
#        DiskFlashback/readwrite_dms.h
#        DiskFlashback/readwrite_file.cpp
#        DiskFlashback/readwrite_file.h
        DiskFlashback/readwrite_floppybridge.cpp
        DiskFlashback/readwrite_floppybridge.h
#        DiskFlashback/readwrite_mfm.cpp
#        DiskFlashback/readwrite_mfm.h
#        DiskFlashback/SCPFile.cpp
#        DiskFlashback/SCPFile.h
        DiskFlashback/sectorCache.cpp
        DiskFlashback/sectorCache.h
        DiskFlashback/sectorCommon.h
)
target_include_directories(diskflashback PRIVATE DiskFlashback ${LIBSAFEC_INCLUDE_DIRS})
target_link_directories(diskflashback PUBLIC ${LIBSAFEC_LIBRARY_DIRS})
target_link_libraries(diskflashback PUBLIC fatfs ${LIBSAFEC_LIBRARIES})

add_library(floppybridge
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
)
target_include_directories(floppybridge PRIVATE floppybridge)

configure_file(fusefatfs/config.h.in fusefatfs/config.h @ONLY)
add_executable(gwmount
        fusefatfs/fftable.c
        fusefatfs/fftable.h
        fusefatfs/fusefatfs.c
)
target_link_libraries(gwmount fatfs diskflashback ${FUSE_LIBRARIES})
target_link_directories(gwmount PRIVATE ${FUSE_LIBRARY_DIRS})
target_include_directories(gwmount PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fusefatfs)
target_compile_definitions(gwmount PRIVATE -D_FILE_OFFSET_BITS=64)
