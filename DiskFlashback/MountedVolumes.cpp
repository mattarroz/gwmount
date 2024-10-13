
#pragma region FATFS

#include "sectorCache.h"

#include <diskio.h>
#include <safe_lib.h>

// I hate this being here
static SectorCacheEngine* fatfsSectorCache = nullptr;
void setFatFSSectorCache(SectorCacheEngine* _fatfsSectorCache) {
  fatfsSectorCache = _fatfsSectorCache;
}
DSTATUS disk_status(BYTE pdrv) {
  if (fatfsSectorCache && (pdrv == 0)) {
    if (!fatfsSectorCache->isDiskPresent()) return STA_NODISK;
    if (fatfsSectorCache->isDiskWriteProtected()) return STA_PROTECT;
    return 0;
  }
  return STA_NOINIT;
}
DSTATUS disk_initialize(BYTE pdrv) {
  if (fatfsSectorCache && (pdrv==0)) {
    if (!fatfsSectorCache->isDiskPresent()) return STA_NODISK;
    if (fatfsSectorCache->isDiskWriteProtected()) return STA_PROTECT;
    return 0;
  }
  return STA_NOINIT;
}
DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
  if (fatfsSectorCache && (pdrv == 0)) {
    if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;

    while (count) {
      if (!fatfsSectorCache->hybridReadData(sector, fatfsSectorCache->sectorSize(), buff))
	return RES_ERROR;
      count--;
      sector++;
      buff += fatfsSectorCache->hybridSectorSize();
    }
    return RES_OK;
  }
  return RES_PARERR;
}
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
  if (fatfsSectorCache && (pdrv == 0)) {
    if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;
    if (fatfsSectorCache->isDiskWriteProtected()) return RES_WRPRT;
    while (count) {
      if (!fatfsSectorCache->writeData(sector, fatfsSectorCache->sectorSize(), buff))
	return RES_ERROR;

      count--;
      sector++;
      buff += fatfsSectorCache->sectorSize();
    }
    return RES_OK;
  }
  return RES_PARERR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
  if (fatfsSectorCache && (pdrv == 0)) {
    if (!fatfsSectorCache->isDiskPresent()) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC: // Complete pending write process (needed at FF_FS_READONLY == 0)
      if (!fatfsSectorCache->flushWriteCache()) return RES_ERROR;
      return RES_OK;

    case GET_SECTOR_COUNT: // Get media size (needed at FF_USE_MKFS == 1)
      *((uint32_t*)buff) = fatfsSectorCache->hybridNumSectorsPerTrack() * fatfsSectorCache->hybridTotalNumTracks();
      return RES_OK;

    case GET_SECTOR_SIZE: // Get sector size (needed at FF_MAX_SS != FF_MIN_SS)
      *((uint32_t*)buff) = fatfsSectorCache->hybridSectorSize();
      return RES_OK;

    case GET_BLOCK_SIZE: // Get erase block size (needed at FF_USE_MKFS == 1)
      *((uint32_t*)buff) = 1;
      return RES_OK;
    }
  }
  return RES_PARERR;
}
uint32_t get_fattime(void) {
  tm stm;
  time_t t = time(0);
  localtime_s(& t, &stm);
  return (uint32_t)(stm.tm_year - 80) << 25 | (uint32_t)(stm.tm_mon + 1) << 21 | (uint32_t)stm.tm_mday << 16 | (uint32_t)stm.tm_hour << 11 | (uint32_t)stm.tm_min << 5 | (uint32_t)stm.tm_sec >> 1;
}