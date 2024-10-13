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

#include <vector>
#include <thread>
//#include "SignalWnd.h"
#include "MountedVolume.h"
#include "sectorCache.h"
#include <ff.h>



class VolumeManager {
private:
	// Full path to main exe name (this program)
	const std::wstring m_mainExeFilename;
	std::wstring m_mountMode;
	SectorType m_currentSectorFormat;
	bool m_forceReadOnly;
	bool m_triggerExplorer = false;
	bool m_ejecting = false;
	bool m_autoRename;

	// If we have an Amiga disk inserted
	FATFS* m_fatDevice = nullptr;


	// The device or file
	SectorCacheEngine* m_io = nullptr;

	// Currently mounted volumes
	std::vector<MountedVolume*> m_volumes;

	// The first letter to start mounting from
	WCHAR m_firstDriveLetter;

	// Active threads we're keeping track of
	std::vector<std::thread> m_threads;

	// Special version to call adfDevMount that monitors the cylinder count for weirdness
	void adfDevMountCylinders();

	// General cleanup and monitoring to see if file systems have dropped out
	void checkRunningFileSystems();

	// Handle a request to copy a file to the active drive
	int handleCopyToDiskRequest(const std::wstring message);

	// Searches active mounted volumes and looks for one with a drive letter and returns it
	MountedVolume* findVolumeFromDriveLetter(const WCHAR driveLetter);

	// Notification received that the current disk changed
	void diskChanged(bool diskInserted, SectorType diskFormat); 

	// triggers the re-mounting of a disk
	void triggerRemount();

	// clean up threads
	void cleanThreads();

	// Mount the new amiga volumes
	uint32_t mountAmigaVolumes(uint32_t startPoint);

	// start mounting IBM volumes using m_volume[startPoint]
	uint32_t mountIBMVolumes(uint32_t startPoint);

	// Start any volumes that aren't running
	void startVolumes();
public:
 	VolumeManager (const std::wstring &mainExe, WCHAR firstDriveLetter, bool forceReadOnly);
	~VolumeManager();

	// Start a file mount
	bool mountFile(const std::wstring& filename);

	// Mount a raw volume
	bool mountRaw(const std::wstring& physicalDrive, bool readOnly);

	// Start a drive mount
	bool mountDrive(const std::wstring& floppyProfile);

	// Main loop
	bool run(bool triggerExplorer = false);

	// Refresh the window title
	void refreshWindowTitle();

	// Actually unmount anything mounted
	void unmountPhysicalFileSystems();

	// Return TRUE if auto renaming is enabled
	bool autoRename() { return m_autoRename; };

};


void adfPrepNativeDriver();
void setFatFSSectorCache(SectorCacheEngine* _fatfsSectorCache);