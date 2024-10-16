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

#include "sectorCache.h"
#include <safe_mem_lib.h>

// Get oldest sector we've cached and remove it, but don't free it!
SectorCacheEngine::SectorData* SectorCacheEngine::getAndReleaseOldestSector() {
    SectorData* result = nullptr;
    uint32_t sectorNumber = 0;

    for (auto cache : m_cache)
        if ((!result) || (cache.second->lastUse < result->lastUse)) {
            result = cache.second;
            sectorNumber = cache.first;
        }
    if (!result) return nullptr;

    m_cache.erase(sectorNumber);
    return result;
}


// Write data to the cache
void SectorCacheEngine::writeCache(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    if (!m_cacheMaxMem) return;

    auto f = m_cache.find(sectorNumber);
    SectorData* secData;

    if (f == m_cache.end()) {
        if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize; // approx

        // If cache size is ZERO it means cache everything
        if (m_cache.size() > m_maxCacheEntries) {
            secData = getAndReleaseOldestSector();
            if (!secData) return;
            if (secData->sectorSize != sectorSize) {
                free(secData->data);
                secData->data = malloc(sectorSize);
                secData->sectorSize = sectorSize;
            }
        }
        else {
            secData = new SectorData();
            if (!secData) return;
            secData->data = malloc(sectorSize);
            secData->sectorSize = sectorSize;
            if (!secData->data) {
                delete secData;
                return;
            }
        }
        m_cache.insert(std::make_pair(sectorNumber, secData));
    }
    else {
        secData = f->second;
    }

    // Make a copy
    memcpy_s(secData->data, sectorSize, data, sectorSize);
    secData->lastUse = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
}

// Read data from the cache
bool SectorCacheEngine::readCache(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    if (!m_cacheMaxMem) return false;

    auto f = m_cache.find(sectorNumber);
    if (f == m_cache.end()) return false;
    if (m_maxCacheEntries == 0) m_maxCacheEntries = m_cacheMaxMem / sectorSize;

    memcpy_s(data, sectorSize, f->second->data, std::min(sectorSize, f->second->sectorSize));
    f->second->lastUse = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    return true;
}

// Reset the cache
void SectorCacheEngine::resetCache() {
    for (auto it : m_cache) {
        if (it.second->data) free(it.second->data);
        delete it.second;
    }
    m_cache.clear();
}

SectorCacheEngine::SectorCacheEngine(const uint32_t maxCacheMem) : m_maxCacheEntries(0), m_cacheMaxMem(maxCacheMem) {

}

SectorCacheEngine::~SectorCacheEngine() {
    resetCache();
}

bool SectorCacheEngine::hybridReadData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) { 
    std::lock_guard lock(m_multithreadLock);
    return internalHybridReadData(sectorNumber, sectorSize, data); 
};

bool SectorCacheEngine::readData(const uint32_t sectorNumber, const uint32_t sectorSize, void* data) {
    std::lock_guard lock(m_multithreadLock);

    if (readCache(sectorNumber, sectorSize, data)) return true;

    if (internalReadData(sectorNumber, sectorSize, data)) {
        writeCache(sectorNumber, sectorSize, data);
        return true;
    }

    return false;
}

bool SectorCacheEngine::writeData(const uint32_t sectorNumber, const uint32_t sectorSize, const void* data) {
    std::lock_guard lock(m_multithreadLock);

    if (internalWriteData(sectorNumber, sectorSize, data)) {
        writeCache(sectorNumber, sectorSize, data);
        return true;
    } 
    return false;
}

