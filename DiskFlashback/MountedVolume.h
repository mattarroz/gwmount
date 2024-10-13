/* DiskFlashback, Copyright (C) 2021-2024 Robert Smith (@RobSmithDev)
 * https://robsmithdev.co.uk/diskflashback
 *
 * This file is multi-licensed under the terms of the Mozilla Public
 * License Version 2.0 as published by Mozilla Corporation and the
 * GNU General Public License, version 2 or later, as published by the
 * Free Software Foundation.
 *
 * MPL2: https://www.mozilla.org/en-US/MPL/2.0/
 * GPL2: https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 *
 * This file is maintained at https://github.com/RobSmithDev/DiskFlashback
 */

#pragma once

//#include "adf_operations.h"
//#include "pfs3_operations.h"
//#include "fat_operations.h"
#include "../fatfs/source/ff.h"
#include <ff.h>
#include <string>
//#include "PFS3lib/pfs3.h"
//#include "adf_nativedriver.h"


class ShellRegistery;
class VolumeManager;
class SectorCacheEngine;

class MountedVolume {
private:
	VolumeManager* m_manager; 
    SectorCacheEngine* m_io = nullptr;
    uint32_t m_partitionIndex = 0;

    bool m_tempUnmount = false;
    ShellRegistery* m_registry;
    FATFS* m_FatFS = nullptr;
protected:
    virtual bool isForcedWriteProtect() ;
public:
	MountedVolume(VolumeManager* manager, const std::wstring& mainEXE, SectorCacheEngine* io, const WCHAR driveLetter, const bool forceWriteProtect);
    virtual ~MountedVolume();

    // Mount a Fat12 device
    bool mountFileSystem(FATFS* ftFSDevice, uint32_t partitionIndex, bool showExplorer);

    // Unmount *any* file system 
	void unmountFileSystem();

    virtual bool isDiskInDrive() ;
    virtual bool isDriveLocked() ;
    virtual bool isWriteProtected() ;
    virtual uint32_t volumeSerial() ;
    virtual const std::wstring getDriverName() ;
    virtual SectorCacheEngine* getBlockDevice() ;
    virtual bool isPhysicalDevice() ;
    virtual void temporaryUnmountDrive();
    virtual void restoreUnmountedDrive(bool restorePreviousSystem);
    virtual uint32_t getTotalTracks() ;

    // Set if the system recognised the sector format even if it didnt understand the disk
    void setSystemRecognisedSectorFormat(bool wasRecognised);

    // Returns FALSE if files are open
    bool setLocked(bool enableLock);

    // Install bootblock for Amiga drives
    bool installAmigaBootBlock();

    // Refresh the auto rename
    void refreshRenameSettings();

    // Shut down the file system
    virtual void shutdownFS() ;
};

