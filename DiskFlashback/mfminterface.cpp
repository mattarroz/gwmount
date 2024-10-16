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

#include "mfminterface.h"
#include "amiga_sectors.h"
#include "ibm_sectors.h"
#include <csignal>
#include <cstring>
#include <safe_mem_lib.h>
#include <stdio.h>

void SectorCacheMFM::releaseDrive() {
    if (m_diskInDrive) {
        m_diskInDrive = false;
        if (m_diskChangeCallback) m_diskChangeCallback(false, SectorType::stUnknown);
        m_diskType = SectorType::stUnknown;
    }
}

// init the drive
bool SectorCacheMFM::initDrive() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_diskType = SectorType::stUnknown;
    m_motorTurnOnTime = 0;
    m_diskInDrive = false;
    m_alwaysIgnore = false;

    if (!restoreDrive()) 
        return false;

    return true;
}

// Reset the cache
void SectorCacheMFM::resetCache() {
    SectorCacheEngine::resetCache();

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_tracksToFlush.clear();
    for (uint32_t systems = 0; systems < 2; systems++)
        for (DecodedTrack& trk : m_trackCache[systems]) trk.sectors.clear();
}

// Flush changes to disk
bool SectorCacheMFM::flushWriteCache() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return flushPendingWrites();
}

// Constructor
SectorCacheMFM::SectorCacheMFM(std::function<void(bool diskInserted, SectorType diskFormat)> diskChangeCallback) :
    SectorCacheEngine(0), m_motorTurnOnTime(0), m_timer(0), m_diskChangeCallback(diskChangeCallback) {

    m_mfmBuffer = malloc(MAX_TRACK_SIZE);
    if (!m_mfmBuffer) return;
}

void SectorCacheMFM::setReady() {
    initDrive();

    if (isDiskInDrive()) identifyFileSystem();

    // Force m_sectorsPerTrack to be populated
    {
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

//      	timer_t timerid;
//	sigevent sev;
//	if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
//	  perror("timer_create");
//	  exit(EXIT_FAILURE);
//	}
//
//	m_timerQueue.push_back (timerid);
//        CreateTimerQueueTimer(&m_timer, m_timerQueue, MotorMonitor, this, 1000, 200, WT_EXECUTEDEFAULT | WT_EXECUTELONGFUNCTION);
    }

}

// Reads some data to see what kind of disk it is
void SectorCacheMFM::identifyFileSystem() {
    if (!m_fileSystemID) return;

    for (uint32_t i = 0; i < 2; i++) {
        m_totalCylinders[i] = 0;
        m_numHeads[i] = 2;
    }
    m_alwaysIgnore = false;
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    m_diskType = SectorType::stUnknown;
    cylinderSeek(0, false);
    motorInUse(true);
    if (waitForMotor(false)) {
        for (uint32_t retries = 0; retries < 5; retries++) {
            if (doTrackReading(0, 0, false))
                if (m_diskType != SectorType::stUnknown)
                    break;
        }
    }
}


// Return TRUE if theres a disk in the drive
bool SectorCacheMFM::isDiskPresent() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return m_diskInDrive;
}

// Return TRUE if the disk is write protected
bool SectorCacheMFM::isDiskWriteProtected() {
    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
    return isDriveWriteProtected();
}

// Return TRUE if you can export this to disk image
bool SectorCacheMFM::allowCopyToFile() {
    return (m_diskType == SectorType::stAmiga) || (m_diskType == SectorType::stIBM);
}

// Override sector infomration
void SectorCacheMFM::overwriteSectorSettings(const SectorType systemType, const uint32_t totalCylinders, const uint32_t totalHeads, const uint32_t sectorsPerTrack, const uint32_t sectorSize) {
    {
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
        m_sectorsPerTrack[0] = sectorsPerTrack;
        m_bytesPerSector[0] = sectorSize;
        m_totalCylinders[0] = std::min((unsigned)totalCylinders,(unsigned) MAX_TRACKS / 2);
        m_numHeads[0] = totalHeads;
        m_diskType = systemType;
        m_tracksToFlush.clear();
    }
    resetCache();
}

// trigger new disk detection
void SectorCacheMFM::triggerNewDiskMount() {
    resetCache();
    m_diskType = SectorType::stUnknown;
    m_diskInDrive = false;
}

// Pre-populate with blank sectors
void SectorCacheMFM::createBlankSectors() {
    DecodedSector blankSector;
    blankSector.numErrors = 0;
    blankSector.data.resize(m_bytesPerSector[0]);

    for (uint32_t trk = 0; trk < m_totalCylinders[0] * m_numHeads[0]; trk++) {
        m_trackCache[0][trk].sectorsWithErrors = 0;
        m_trackCache[0][trk].sectors.clear();
        for (uint32_t sec = 0; sec < m_sectorsPerTrack[0]; sec++)
            m_trackCache[0][trk].sectors.insert(std::make_pair(sec, blankSector));
    }
}

uint64_t GetTickCount64() {
  using namespace std::chrono;
  auto now = steady_clock::now();
  auto duration = now.time_since_epoch();
  return duration_cast<milliseconds>(duration).count();
}

// The motor usage has timed out
void SectorCacheMFM::motorMonitor() {
    bool sendNotify = false;
    {
        // Shoudl it time out?
        std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);
        if ((m_motorTurnOnTime) && (GetTickCount64() - m_motorTurnOnTime > MOTOR_IDLE_TIMEOUT)) {
            flushPendingWrites();
            motorEnable(false, false);
            if (!m_alwaysIgnore) m_ignoreErrors = false;
            m_blockWriting = false;
            m_motorTurnOnTime = 0;
        }

        // Force writing etc
        const bool isDiskNowInDrive = isDiskInDrive();
        if (isDiskNowInDrive != m_diskInDrive) {
            if (!isDiskNowInDrive) {
                cylinderSeek(0, false);
                motorEnable(false, false);

                if (m_tracksToFlush.size()) {
		  // FIXME(mreis) removed code here diskRemovedWarning
                    m_tracksToFlush.clear();
                }

                // cache really needs to be cleared!
                if (m_tracksToFlush.size() < 1) {
                    for (uint32_t trk = 0; trk < MAX_TRACKS; trk++) {
                        m_trackCache[0][trk].sectors.clear();
                        m_trackCache[0][trk].sectors.clear();
                    }
                }
            }
            m_diskInDrive = isDiskNowInDrive;
            sendNotify = true;
        }
    }

    if (sendNotify) {
        if (!m_fileSystemID) return;
        if (m_diskChangeCallback) {
            for (DecodedTrack& trk : m_trackCache[0]) trk.sectors.clear();
            for (DecodedTrack& trk : m_trackCache[1]) trk.sectors.clear();
            if (m_diskInDrive) 
                identifyFileSystem(); 
            else m_diskType = SectorType::stUnknown;
            m_diskChangeCallback(m_diskInDrive, m_diskInDrive ? m_diskType : SectorType::stUnknown);
        }
    }
}

// Signal the motor is in use.  Returns if its ok
void SectorCacheMFM::motorInUse(bool upperSide) {
    if (!m_motorTurnOnTime) motorEnable(true, upperSide);
    m_motorTurnOnTime = GetTickCount64();
}

// Waits for the drive to be ready, and if it times out, returns false
bool SectorCacheMFM::waitForMotor(bool upperSide) {
    motorInUse(upperSide);
    while (!motorReady()) {
        sleep(100);
        if (GetTickCount64() - m_motorTurnOnTime > MOTOR_TIMEOUT_TIME) 
            return false;
        motorInUse(upperSide);
    }
    return true;
}

// Release
SectorCacheMFM::~SectorCacheMFM() {
    releaseDrive();

    // FLUSH
    std::lock_guard<std::mutex> guard(m_motorTimerProtect);
//    if (m_timer) {
//        // Disable the motor timer
//        DeleteTimerQueueTimer(m_timerQueue, m_timer, 0);
//        m_timer = 0;
//    }
//    if (DeleteTimerQueueEx(m_timerQueue, NULL)) m_timerQueue = 0;

    if (m_mfmBuffer) free(m_mfmBuffer);
}

// Get size of the disk in bytes
uint64_t SectorCacheMFM::getDiskDataSize() {
    if (!available()) return 0;
    if (m_totalCylinders[0]) return m_bytesPerSector[0] * m_sectorsPerTrack[0] * m_numHeads[0] * m_totalCylinders[0];
    return m_bytesPerSector[0] * m_sectorsPerTrack[0] * m_numHeads[0] * 82;
}

// Read *all* data from a specific cylinder and side
bool SectorCacheMFM::readDataAllFS(const uint32_t fileSystem, const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (sectorSize != m_bytesPerSector[fileSystem])
        return false;

    const int track = sectorNumber / m_sectorsPerTrack[fileSystem];
    const int trackBlock = sectorNumber % m_sectorsPerTrack[fileSystem];
    const bool upperSurface = track % m_numHeads[fileSystem];
    const int cylinder = track / m_numHeads[fileSystem];

    if (track >= MAX_TRACKS)
        return false;

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    checkFlushPendingWrites();

    if (!isDiskInDrive()) return false;

    // Retry several times
    uint32_t retries = 0;
    for (;;) {
        // First, see if we have a perfect sector already
        auto it = m_trackCache[fileSystem][track].sectors.find(trackBlock);

        if (it != m_trackCache[fileSystem][track].sectors.end()) {
            // No errors? (or are we skipping them?)
            if ((it->second.numErrors == 0) || (m_ignoreErrors)) {
                memcpy_s(data, sectorSize, it->second.data.data(), std::min((unsigned)it->second.data.size(), (unsigned)sectorSize));
                return true;
            }
        }

        // Retry monitor
        if (retries > MAX_RETRIES) {
            if (m_ignoreErrors) return false;
            retries = 0;

            if (!shouldPrompt()) return false;

	    fprintf(stderr, "Disk read errors were detected. What would you like to do? \n Retry (r), Ignore (i), Always Ignore (a), Quit (q)");
            switch (getchar()) {
            case 'r': break;
            case 'i': m_ignoreErrors = true;
                break;
            case 'a': m_alwaysIgnore = true;
                m_ignoreErrors = true;
                break;
            default: 
                return false;
            }

            // Re-check disk is actually inserted!
            if (!isDiskInDrive()) return false;
        }

        // If this hits, then do a re-seek.  Sometimes it helps
        if (retries == MAX_RETRIES / 2) {
            if (!isDiskInDrive()) return false;
            motorInUse(upperSurface);
            if (isPhysicalDisk()) {
                if (cylinder < 40)
                    cylinderSeek(79, upperSurface);
                else
                    cylinderSeek(0, upperSurface);

                // Wait for the seek, or it will get removed! 
                sleep(300);
            }
            if (!isDiskInDrive()) return false;
        }

        // If we get here then this sector isn't in the cache (or has errors), so we'll read and update ALL sectors for this cylinder
        motorInUse(upperSurface);
        cylinderSeek(cylinder, upperSurface);

        // Wait for the motor to spin up properly
        if (!waitForMotor(upperSurface))
            return false;

        // Actually do the read
        doTrackReading(fileSystem, track, retries > 1);

        retries++;
    }

    return true;
}

// Do reading
bool SectorCacheMFM::internalReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (sectorSize != m_bytesPerSector[0]) return false;

    return readDataAllFS(0, sectorNumber, sectorSize, data);
}

// Do reading
bool SectorCacheMFM::internalHybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    uint32_t fs = (m_diskType == SectorType::stHybrid) ? 1 : 0;

    if (sectorSize != m_bytesPerSector[fs]) return false;

    return readDataAllFS(fs, sectorNumber, sectorSize, data);
}


// Internal single attempt to read a track
bool SectorCacheMFM::doTrackReading(const uint32_t fileSystem, const uint32_t track, bool retryMode) {
    // Read some track data, with some delay for a retry
    uint64_t start = GetTickCount64();
    uint32_t bitsReceived;
    do {
        motorInUse(track % m_numHeads[fileSystem]);
        // Try both methods
        if (fileSystem == 1) {  // Hybrid file system
            bitsReceived = mfmRead(track * ((m_numHeads[fileSystem]==1) ? 2 : 1), retryMode, m_mfmBuffer, MAX_TRACK_SIZE);
        } else bitsReceived = mfmRead(track, retryMode, m_mfmBuffer, MAX_TRACK_SIZE);
        if (!bitsReceived) bitsReceived = mfmRead(track / m_numHeads[fileSystem], track % m_numHeads[fileSystem], retryMode, m_mfmBuffer, MAX_TRACK_SIZE);

        if (!bitsReceived) {
            if (GetTickCount64() - start > TRACK_READ_TIMEOUT) return false;
            else sleep(50);
        }
    } while (!bitsReceived);

    // Try to identify the file system
    if (m_diskType == SectorType::stUnknown) {
        // Some defaults
        m_serialNumber[0] = 0x554E4B4E;
        m_serialNumber[1] = 0x554E4B4E;
        m_numHeads[0] = 2;
        m_numHeads[1] = 2;
        getTrackDetails_AMIGA(isHD(), m_sectorsPerTrack[0], m_bytesPerSector[0]);
        DecodedTrack trAmiga;
        findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, 0, trAmiga);
        DecodedTrack trIBM;
        bool nonStandard = false;
        findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, 0, trIBM, nonStandard);
        uint32_t serialNumber;
        uint32_t sectorsPerTrack;
        uint32_t bytesPerSector;

        if (trAmiga.sectors.size()) {
            m_diskType = SectorType::stAmiga;
            m_sectorsPerTrack[0] = std::max(m_sectorsPerTrack[0], (uint32_t)trAmiga.sectors.size());
            m_serialNumber[0] = 0x414D4644; // AMFD
        }
        else m_diskType = SectorType::stUnknown;

        if (trIBM.sectors.size() >= 5) {
            m_diskType = SectorType::stIBM;
            uint32_t totalSectors;
            uint32_t numHeads;
            if (getTrackDetails_IBM(&trIBM, serialNumber, numHeads, totalSectors, sectorsPerTrack, bytesPerSector)) {
                if ((trIBM.sectors.size() >= 5) && (trAmiga.sectors.size() > 1)) {
                    m_diskType = SectorType::stHybrid;
                }
                else
                    if (nonStandard /*|| (numHeads < 2)*/) m_diskType = SectorType::stAtari;
                uint32_t i = (m_diskType == SectorType::stHybrid) ? 1 : 0;
                m_sectorsPerTrack[i] = sectorsPerTrack;
                m_bytesPerSector[i] = bytesPerSector;
                m_serialNumber[i] = serialNumber;
                m_numHeads[i] = numHeads;
                m_totalCylinders[i] = std::max((unsigned)80, (totalSectors / sectorsPerTrack) / m_numHeads[i]);
            }
            else {
                m_sectorsPerTrack[0] = isHD() ? 18 : 9;
                m_bytesPerSector[0] = 512;
                m_serialNumber[0] = 0xAAAAAAAA;
                m_totalCylinders[0] = 80;
                m_numHeads[0] = 2;
            }
        }
    }
    if (m_diskType == SectorType::stHybrid) {

        if (m_numHeads[1] == 2) {  // Has 2 sides? Treat everything as normal
            findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
            findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track]);
        }
        else // Atari is single sided. Amiga is ALWAYS double sided
            if (fileSystem == 1) {
                findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track * 2, m_sectorsPerTrack[0], m_trackCache[0][track * 2]);
                findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track]);
            }
            else {
                findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
                if ((track & 1) == 0)
                    findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[1], m_trackCache[1][track >> 1]);
            }
    }
    else
        if (m_diskType == SectorType::stAmiga)
            findSectors_AMIGA((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);
    if ((m_diskType == SectorType::stAtari) || (m_diskType == SectorType::stIBM))
        findSectors_IBM((const unsigned char*)m_mfmBuffer, bitsReceived, isHD(), track, m_sectorsPerTrack[0], m_trackCache[0][track]);

    return true;
}

// Do writing
bool SectorCacheMFM::internalWriteData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (m_blockWriting) return false;
    if ((m_diskType == SectorType::stHybrid) || (m_diskType == SectorType::stUnknown)) return false;
    if (isDiskWriteProtected()) return false;

    const int track = sectorNumber / m_sectorsPerTrack[0];
    if (track >= MAX_TRACKS) return false;
    const int trackBlock = sectorNumber % m_sectorsPerTrack[0];
    const bool upperSurface = track % m_numHeads[0];
    const int cylinder = track / m_numHeads[0];

    std::lock_guard<std::mutex> bridgeLock(m_motorTimerProtect);

    // Now replace the sector we're overwriting, just in memory at this point
    auto it = m_trackCache[0][track].sectors.find(trackBlock);
    if (it != m_trackCache[0][track].sectors.end()) {
        if (memcmp(it->second.data.data(), data, std::min(sectorSize, (unsigned)it->second.data.size())) == 0) {
            if (it->second.numErrors == 0) return true;
            it->second.numErrors = 0;                
        }
        else {
            // No errors? (or are we skipping them?)
            memcpy_s(it->second.data.data(), it->second.data.size(), data, std::min(sectorSize, (unsigned)it->second.data.size()));
            it->second.numErrors = 0;
        }
    }
    else {
        // Add the track
        DecodedSector sector;
        sector.data.resize(m_bytesPerSector[0]);
        memcpy_s(sector.data.data(), sector.data.size(), data, std::min((unsigned)sector.data.size(), sectorSize));
        sector.numErrors = 0;
        m_trackCache[0][track].sectors.insert(std::make_pair(trackBlock, sector));
    }

    auto i = m_tracksToFlush.find(track);
    if (i == m_tracksToFlush.end())
        m_tracksToFlush.insert(std::make_pair(track, 1));
    else i->second++;

    motorInUse(upperSurface);
    checkFlushPendingWrites();

    return true;
}

// Checks for pending writes, if theres too many then flush them
void SectorCacheMFM::checkFlushPendingWrites() {
    if (m_tracksToFlush.size() < FORCE_FLUSH_AT_TRACKS) return;
    flushPendingWrites();
}

// Removes anything that failed from the cache so it has to be re-read from the disk
void SectorCacheMFM::removeFailedWritesFromCache() {
    for (auto& trk : m_tracksToFlush)
        if (trk.second)
            m_trackCache[0][trk.first].sectors.clear();
    m_tracksToFlush.clear();
}

// Flush any writing thats still pending - lock must already be obtained
bool SectorCacheMFM::flushPendingWrites() {
    if (m_blockWriting) return false;

    for (auto& trk : m_tracksToFlush) {
        const uint32_t track = trk.first;
        const bool upperSurface = track % m_numHeads[0];
        const int cylinder = track / m_numHeads[0];

        // Motor shouldn't stop here
        motorInUse(upperSurface);
        cylinderSeek(cylinder, upperSurface);
        if (!waitForMotor(upperSurface)) {
            m_tracksToFlush.clear();
            return false;
        }
        cylinderSeek(cylinder, upperSurface);

        // Assemble and commit an entire track.  First see if any data is missing
        bool fillData = m_trackCache[0][track].sectors.size() < m_sectorsPerTrack[0];
        if (!fillData)
            for (const auto& it : m_trackCache[0][track].sectors)
                if (it.second.numErrors) {
                    fillData = true;
                    break;
                }

        // Theres some missing data. We we'll request the track again and fill in the gaps
        if (fillData) {
            // 1. Take a copy
            const std::map<int, DecodedSector> backup = m_trackCache[0][track].sectors;
            if (m_writeOnly) {
                for (uint32_t sec = 0; sec < m_sectorsPerTrack[0]; sec++) {
                    auto it = m_trackCache[0][track].sectors.find(sec);
                    // Does a sector with this number exist?
                    if (it == m_trackCache[0][track].sectors.end()) {
                        DecodedSector tmp;
                        tmp.numErrors = 0;
                        tmp.data.resize(m_bytesPerSector[0]);;
                        m_trackCache[0][track].sectors.insert(std::make_pair(sec, tmp));
                    }
                }
            }
            else {
                // 2. *try* to read the track (but dont care if it fails)
                doTrackReading(0, track, false);
            }
            // 3. Replace any tracks now read with any we have in our backup that have errors = 0
            for (const auto& sec : backup) {
                if (sec.second.numErrors == 0) {
                    auto it = m_trackCache[0][track].sectors.find(sec.first);
                    if (it != m_trackCache[0][track].sectors.end())
                        it->second = sec.second;
                }
            }
        }

        // Remove sectors that shouldn't be there
        while (m_trackCache[0][track].sectors.size() > m_sectorsPerTrack[0]) 
            m_trackCache[0][track].sectors.erase(m_trackCache[0][track].sectors.rbegin()->first);

        // We will now have a complete track worth of sectors so we can now finally commit this to disk (hopefully) plus we will verify it
        uint32_t numBytes;

        switch (m_diskType) {
        case SectorType::stAmiga: numBytes = encodeSectorsIntoMFM_AMIGA(isHD(), m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stIBM: numBytes = encodeSectorsIntoMFM_IBM(isHD(), false, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stAtari: numBytes = encodeSectorsIntoMFM_IBM(isHD(), true, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer); break;
        case SectorType::stHybrid:
            // Need to work out which type of track it is although technically hybrid isnt supported for writing
            if ((m_trackCache[0][track].sectors.size() == 11) || (m_trackCache[0][track].sectors.size() == 22))
                numBytes = encodeSectorsIntoMFM_AMIGA(isHD(), m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer);
            else numBytes = encodeSectorsIntoMFM_IBM(isHD(), true, &m_trackCache[0][track], track, MAX_TRACK_SIZE, m_mfmBuffer);
            break;
        default:
            numBytes = 0;
            break;
        }

        if (numBytes < 0) {
            // this *should* never happen
            removeFailedWritesFromCache();
            return false;
        }

        // Write retries
        uint32_t retries = 0;
        for (;;) {
            // Handle a re-seek - might clean the head
            if (retries == MAX_RETRIES / 2) {
                if (isPhysicalDisk()) {
                    motorInUse(upperSurface);

                    if (cylinder < 40)
                        cylinderSeek(79, upperSurface);
                    else
                        cylinderSeek(0, upperSurface);

                    // Wait for the seek, or it will get removed! 
                    sleep(300);
                }
                retries = 0;
            }
            cylinderSeek(cylinder, upperSurface);

            // Commit to disk
            motorInUse(upperSurface);

            if (!isDiskInDrive()) {
//                if (!diskRemovedWarning()) {
//                    removeFailedWritesFromCache();
//                    return false;
//                }
// FIXME(mreis):
		  fprintf(stderr, "disk not in drive\n");
		  return false;
            }

            if (mfmWrite(cylinder, upperSurface, (m_diskType == SectorType::stIBM) || (m_diskType == SectorType::stAtari), m_mfmBuffer, numBytes )) {
                // Now wait until it completes - approx 400-500ms as this will also read it back to verify it
                uint64_t start = GetTickCount64();
                bool doRetry = false;
                while (!writeCompleted()) {
                    if (GetTickCount64() - start > DISK_WRITE_TIMEOUT) {
                        resetDrive(cylinder);
                        m_motorTurnOnTime = 0;
                        
                        if (isPhysicalDisk()) sleep(200);

                        if (!isDiskInDrive()) {
//                            if (diskRemovedWarning()) {
//                                doRetry = true;
//                                break;
//                            }
//                            else {
//                                m_blockWriting = true;
//                                removeFailedWritesFromCache();
//                                return false;
//                            }
// FIXME(mreis)
				fprintf(stderr, "disk not in drive\n");
				return false;
                        }
                        else
                            return false;
                    }
                }
                if (doRetry) {
                    retries = 0;
                }
                else {
                    // Writing succeeded. Now to do a verify!
                    const std::map<int, DecodedSector> backup = m_trackCache[0][track].sectors;
                    for (;;) {
                        if (!doTrackReading(0, track, retries > 1)) {
                            m_motorTurnOnTime = 0;
                            if (!isDiskInDrive()) {
//                                if (!diskRemovedWarning()) {
//                                    removeFailedWritesFromCache();
//                                    return false;
//                                }
// FIXME(mreis):
				fprintf(stderr, "disk not in drive\n");
                            }
                            else
                                return false;
                            // Wait and try again
                            if (isPhysicalDisk()) sleep(100);
                        }
                        else break;
                    }

                    // Check what was read back matches what we wrote
                    bool errors = false;
                    for (const auto& sec : backup) {
                        auto search = m_trackCache[0][track].sectors.find(sec.first);
                        // Sector no longer exists.  ERROR!
                        if (search == m_trackCache[0][track].sectors.end()) {
                            errors = true;
                            break;
                        }
                        else {
                            // Did it reac back with errors!?
                            if (search->second.numErrors) {
                                errors = true;
                                break;
                            }
                            else {
                                // Finally, compare the data and see if its identical
                                if (search->second.data.size() != sec.second.data.size()) {
                                    // BAD read back wrong sector size
                                    errors = true;
                                    break;
                                }
                                else
                                    if (memcmp(search->second.data.data(), sec.second.data.data(), search->second.data.size()) != 0) {
                                        // BAD read back even though there were no errors
                                        errors = true;
                                        break;
                                    }
                            }
                        }
                    }

                    if (!errors) break;
                }
            }
            else {
                // this shouldnt ever happen
                removeFailedWritesFromCache();
                return false;
            }

            retries++;
        }

        // Mark that its done!
        trk.second = 0;
    }

    removeFailedWritesFromCache();
    return true;
}