// SPDX-License-Identifier: FatFs
// Copyright (C) 2025, ChaN, all right reserved.
// C++23 port for UMI framework
//
// Faithfully ported from FatFs R0.15 ff.c by ChaN.
// Configuration: FF_USE_LFN=1 (static BSS), FF_LFN_UNICODE=0 (ANSI/OEM),
//   FF_FS_EXFAT=0, FF_FS_READONLY=0, FF_FS_MINIMIZE=0, FF_FS_RPATH=0,
//   FF_USE_MKFS=0, FF_USE_STRFUNC=0, FF_USE_FIND=0, FF_USE_CHMOD=0,
//   FF_USE_LABEL=0, FF_USE_EXPAND=0, FF_USE_FORWARD=0, FF_FS_LOCK=0,
//   FF_FS_REENTRANT=0, FF_FS_NORTC=1, FF_MIN_SS=FF_MAX_SS=512,
//   FF_CODE_PAGE=437, FF_VOLUMES=1, FF_FS_TINY=0, FF_USE_FASTSEEK=0

#include "ff.hh"
#include "ff_unicode.hh"

#include <cstring>

namespace umi::fs::fat {

// ============================================================================
// Internal constants (from ff.c)
// ============================================================================

// Sector size macros (fixed at 512)
static constexpr uint32_t SS_VAL = 512;

// Name status flags (index 11 of fn[])
static constexpr uint8_t NSFLAG = 11;
static constexpr uint8_t NS_LOSS = 0x01;
static constexpr uint8_t NS_LFN = 0x02;
static constexpr uint8_t NS_LAST = 0x04;
static constexpr uint8_t NS_BODY = 0x08;
static constexpr uint8_t NS_EXT = 0x10;
static constexpr uint8_t NS_DOT = 0x20;
static constexpr uint8_t NS_NOLFN = 0x40;
static constexpr uint8_t NS_NONAME = 0x80;

// FAT sub-type limits
static constexpr uint32_t MAX_FAT12 = 0xFF5;
static constexpr uint32_t MAX_FAT16 = 0xFFF5;
static constexpr uint32_t MAX_FAT32 = 0x0FFFFFF5;

// File access mode internal flags
static constexpr uint8_t FA_SEEKEND = 0x20;
static constexpr uint8_t FA_MODIFIED = 0x40;
static constexpr uint8_t FA_DIRTY = 0x80;

// Attribute mask
static constexpr uint8_t AM_MASK = 0x3F;
static constexpr uint8_t AM_VOL = 0x08;
static constexpr uint8_t AM_LFN = 0x0F;

// Directory entry size
static constexpr uint32_t SZDIRE = 32;

// Directory entry deleted markers
static constexpr uint8_t DDEM = 0xE5;
static constexpr uint8_t RDDEM = 0x05;

// LFN constants
static constexpr uint8_t LLEF = 0x40;   // Last long entry flag

// BPB/BS field offsets
static constexpr uint32_t BS_JmpBoot = 0;
static constexpr uint32_t BS_FilSysType32 = 82;
static constexpr uint32_t BS_55AA = 510;
[[maybe_unused]] static constexpr uint32_t BS_BootSig = 38;
[[maybe_unused]] static constexpr uint32_t BS_VolID = 39;
[[maybe_unused]] static constexpr uint32_t BS_VolID32 = 67;

static constexpr uint32_t BPB_BytsPerSec = 11;
static constexpr uint32_t BPB_SecPerClus = 13;
static constexpr uint32_t BPB_RsvdSecCnt = 14;
static constexpr uint32_t BPB_NumFATs = 16;
static constexpr uint32_t BPB_RootEntCnt = 17;
static constexpr uint32_t BPB_TotSec16 = 19;
static constexpr uint32_t BPB_FATSz16 = 22;
static constexpr uint32_t BPB_TotSec32 = 32;
static constexpr uint32_t BPB_FATSz32 = 36;
static constexpr uint32_t BPB_RootClus32 = 44;
static constexpr uint32_t BPB_FSInfo32 = 48;
static constexpr uint32_t BPB_FSVer32 = 42;

// FSInfo offsets
static constexpr uint32_t FSI_LeadSig = 0;
static constexpr uint32_t FSI_StrucSig = 484;
static constexpr uint32_t FSI_Free_Count = 488;
static constexpr uint32_t FSI_Nxt_Free = 492;
static constexpr uint32_t FSI_TrailSig = 508;

// MBR offsets
static constexpr uint32_t MBR_Table = 446;
static constexpr uint32_t SZ_PTE = 16;
static constexpr uint32_t PTE_StLba = 8;

// Directory entry field offsets
static constexpr uint32_t DIR_Name = 0;
static constexpr uint32_t DIR_Attr = 11;
static constexpr uint32_t DIR_NTres = 12;
static constexpr uint32_t DIR_CrtTime = 14;
static constexpr uint32_t DIR_LstAccDate = 18;
static constexpr uint32_t DIR_FstClusHI = 20;
static constexpr uint32_t DIR_ModTime = 22;
static constexpr uint32_t DIR_FstClusLO = 26;
static constexpr uint32_t DIR_FileSize = 28;

// LFN directory entry offsets
static constexpr uint32_t LDIR_Ord = 0;
static constexpr uint32_t LDIR_Attr = 11;
static constexpr uint32_t LDIR_Type = 12;
static constexpr uint32_t LDIR_Chksum = 13;
static constexpr uint32_t LDIR_FstClusLO = 26;

// Code page
static constexpr uint16_t CODEPAGE = 437;

// Volume ID counter (static, since single instance approach)
static uint16_t Fsid = 0;

// ============================================================================
// Inline helpers
// ============================================================================

static inline uint16_t ld_16(const uint8_t* ptr) {
    return static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
}

static inline uint32_t ld_32(const uint8_t* ptr) {
    return static_cast<uint32_t>(ptr[0]) | (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) | (static_cast<uint32_t>(ptr[3]) << 24);
}

static inline void st_16(uint8_t* ptr, uint16_t val) {
    ptr[0] = static_cast<uint8_t>(val);
    ptr[1] = static_cast<uint8_t>(val >> 8);
}

static inline void st_32(uint8_t* ptr, uint32_t val) {
    ptr[0] = static_cast<uint8_t>(val);
    ptr[1] = static_cast<uint8_t>(val >> 8);
    ptr[2] = static_cast<uint8_t>(val >> 16);
    ptr[3] = static_cast<uint8_t>(val >> 24);
}

// Character type tests
static inline bool IsUpper(uint32_t c) { return c >= 'A' && c <= 'Z'; }
static inline bool IsLower(uint32_t c) { return c >= 'a' && c <= 'z'; }
static inline bool IsDigit(uint32_t c) { return c >= '0' && c <= '9'; }
static inline bool IsSeparator(uint32_t c) { return c == '/' || c == '\\'; }
static inline bool IsTerminator(uint32_t c) { return c < ' '; }

// DBC support (for code page 437, SBCS only — no DBC)
static inline bool dbc_1st(uint8_t /*c*/) { return false; }
[[maybe_unused]] static inline bool dbc_2nd(uint8_t /*c*/) { return false; }

// tchar2uni: get a Unicode character from a TCHAR string (ANSI/OEM input, CP437)
static uint32_t tchar2uni(const char** str) {
    uint32_t uc;
    uc = static_cast<uint8_t>(*(*str)++);
    if (uc >= 0x80) {
        uc = ff_oem2uni(static_cast<uint16_t>(uc), CODEPAGE);
        if (uc == 0) uc = 0xFFFFFFFF;
    }
    return uc;
}

// put_utf: store a Unicode character into a TCHAR string (ANSI/OEM output, CP437)
static uint32_t put_utf(uint32_t chr, char* buf, uint32_t szb) {
    uint16_t wc;
    if (chr < 0x80) {
        if (szb < 1) return 0;
        *buf = static_cast<char>(chr);
        return 1;
    }
    wc = ff_uni2oem(chr, CODEPAGE);
    if (wc == 0) return 0;
    if (wc >= 0x100) {
        if (szb < 2) return 0;
        *buf++ = static_cast<char>(wc >> 8);
        *buf = static_cast<char>(wc);
        return 2;
    } else {
        if (szb < 1) return 0;
        *buf = static_cast<char>(wc);
        return 1;
    }
}

// Upper-case conversion table for extended characters (CP437)
static const uint8_t ExCvt[] = {
    0x80, 0x9A, 0x45, 0x41, 0x8E, 0x41, 0x8F, 0x80, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x8E, 0x8F,
    0x90, 0x92, 0x92, 0x4F, 0x99, 0x4F, 0x55, 0x55, 0x59, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0x41, 0x49, 0x4F, 0x55, 0xA5, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

// ============================================================================
// Macro replacements
// ============================================================================

// LD2PD / LD2PT: volume to physical drive / partition mapping (trivial, single volume)
static inline uint8_t LD2PD(int vol) { (void)vol; return 0; }
static inline uint32_t LD2PT(int vol) { (void)vol; return 0; }

// ============================================================================
// Disk I/O wrappers
// ============================================================================

uint8_t FatFs::disk_initialize(uint8_t pdrv) noexcept {
    (void)pdrv;
    if (diskio && diskio->initialize) return diskio->initialize(diskio->context);
    return STA_NOINIT;
}

uint8_t FatFs::disk_status(uint8_t pdrv) noexcept {
    (void)pdrv;
    if (diskio && diskio->status) return diskio->status(diskio->context);
    return STA_NOINIT;
}

DiskResult FatFs::disk_read(uint8_t pdrv, uint8_t* buff, LBA_t sector, uint32_t count) noexcept {
    (void)pdrv;
    if (diskio && diskio->read) return diskio->read(diskio->context, buff, sector, count);
    return DiskResult::ERROR;
}

DiskResult FatFs::disk_write(uint8_t pdrv, const uint8_t* buff, LBA_t sector, uint32_t count) noexcept {
    (void)pdrv;
    if (diskio && diskio->write) return diskio->write(diskio->context, buff, sector, count);
    return DiskResult::ERROR;
}

DiskResult FatFs::disk_ioctl(uint8_t pdrv, uint8_t cmd, void* buff) noexcept {
    (void)pdrv;
    if (diskio && diskio->ioctl) return diskio->ioctl(diskio->context, cmd, buff);
    return DiskResult::ERROR;
}

// ============================================================================
// get_fattime — fixed timestamp (NORTC mode)
// ============================================================================

uint32_t FatFs::get_fattime() noexcept {
    return (static_cast<uint32_t>(config::NORTC_YEAR - 1980) << 25) |
           (static_cast<uint32_t>(config::NORTC_MON) << 21) |
           (static_cast<uint32_t>(config::NORTC_MDAY) << 16);
}

// ============================================================================
// Window management
// ============================================================================

FatResult FatFs::sync_window(FatFsVolume* fs) noexcept {
    FatResult res = FatResult::OK;
    if (fs->wflag) {
        if (disk_write(fs->pdrv, fs->win, fs->winsect, 1) == DiskResult::OK) {
            fs->wflag = 0;
            if (fs->winsect - fs->fatbase < fs->fsize) {
                // In FAT area — mirror to second FAT
                if (fs->n_fats == 2) disk_write(fs->pdrv, fs->win, fs->winsect + fs->fsize, 1);
            }
        } else {
            res = FatResult::DISK_ERR;
        }
    }
    return res;
}

FatResult FatFs::move_window(FatFsVolume* fs, LBA_t sect) noexcept {
    FatResult res = FatResult::OK;
    if (sect != fs->winsect) {
        res = sync_window(fs);
        if (res == FatResult::OK) {
            if (disk_read(fs->pdrv, fs->win, sect, 1) != DiskResult::OK) {
                sect = static_cast<LBA_t>(0) - 1;
                res = FatResult::DISK_ERR;
            }
            fs->winsect = sect;
        }
    }
    return res;
}

// ============================================================================
// Sync filesystem
// ============================================================================

FatResult FatFs::sync_fs(FatFsVolume* fs) noexcept {
    FatResult res = sync_window(fs);
    if (res == FatResult::OK) {
        if (fs->fs_type == FS_FAT32 && fs->fsi_flag == 1) {
            // Update FSInfo sector
            memset(fs->win, 0, SS_VAL);
            st_16(fs->win + BS_55AA, 0xAA55);
            st_32(fs->win + FSI_LeadSig, 0x41615252);
            st_32(fs->win + FSI_StrucSig, 0x61417272);
            st_32(fs->win + FSI_Free_Count, fs->free_clst);
            st_32(fs->win + FSI_Nxt_Free, fs->last_clst);
            st_32(fs->win + FSI_TrailSig, 0xAA550000);
            fs->winsect = fs->volbase + 1;
            disk_write(fs->pdrv, fs->win, fs->winsect, 1);
            fs->fsi_flag = 0;
        }
        // Make sure the disk is up to date
        if (disk_ioctl(fs->pdrv, CTRL_SYNC, nullptr) != DiskResult::OK) {
            res = FatResult::DISK_ERR;
        }
    }
    return res;
}

// ============================================================================
// Cluster / sector helpers
// ============================================================================

LBA_t FatFs::clst2sect(FatFsVolume* fs, uint32_t clst) noexcept {
    clst -= 2;
    if (clst >= fs->n_fatent - 2) return 0;
    return fs->database + static_cast<LBA_t>(fs->csize) * clst;
}

// ============================================================================
// FAT access
// ============================================================================

uint32_t FatFs::get_fat(FatObjId* obj, uint32_t clst) noexcept {
    uint32_t val;
    FatFsVolume* fs = obj->fs;

    if (clst < 2 || clst >= fs->n_fatent) return 1; // Range check

    switch (fs->fs_type) {
    case FS_FAT12: {
        uint32_t bc = clst;
        bc += bc / 2;
        if (move_window(fs, fs->fatbase + (bc / SS_VAL)) != FatResult::OK) return 0xFFFFFFFF;
        auto wc = fs->win[bc++ % SS_VAL];
        if (move_window(fs, fs->fatbase + (bc / SS_VAL)) != FatResult::OK) return 0xFFFFFFFF;
        wc |= static_cast<uint32_t>(fs->win[bc % SS_VAL]) << 8;
        val = (clst & 1) ? (wc >> 4) : (wc & 0xFFF);
        break;
    }
    case FS_FAT16:
        if (move_window(fs, fs->fatbase + (clst / (SS_VAL / 2))) != FatResult::OK) return 0xFFFFFFFF;
        val = ld_16(fs->win + clst * 2 % SS_VAL);
        break;
    case FS_FAT32:
        if (move_window(fs, fs->fatbase + (clst / (SS_VAL / 4))) != FatResult::OK) return 0xFFFFFFFF;
        val = ld_32(fs->win + clst * 4 % SS_VAL) & 0x0FFFFFFF;
        break;
    default:
        val = 1; // Internal error
        break;
    }
    return val;
}

FatResult FatFs::put_fat(FatFsVolume* fs, uint32_t clst, uint32_t val) noexcept {
    FatResult res = FatResult::INT_ERR;

    if (clst >= 2 && clst < fs->n_fatent) {
        switch (fs->fs_type) {
        case FS_FAT12: {
            uint32_t bc = clst;
            bc += bc / 2;
            res = move_window(fs, fs->fatbase + (bc / SS_VAL));
            if (res != FatResult::OK) break;
            auto* p = &fs->win[bc++ % SS_VAL];
            *p = (clst & 1) ? static_cast<uint8_t>((*p & 0x0F) | ((val << 4) & 0xF0)) : static_cast<uint8_t>(val);
            fs->wflag = 1;
            res = move_window(fs, fs->fatbase + (bc / SS_VAL));
            if (res != FatResult::OK) break;
            p = &fs->win[bc % SS_VAL];
            *p = (clst & 1) ? static_cast<uint8_t>(val >> 4)
                            : static_cast<uint8_t>((*p & 0xF0) | ((val >> 8) & 0x0F));
            fs->wflag = 1;
            break;
        }
        case FS_FAT16:
            res = move_window(fs, fs->fatbase + (clst / (SS_VAL / 2)));
            if (res != FatResult::OK) break;
            st_16(fs->win + clst * 2 % SS_VAL, static_cast<uint16_t>(val));
            fs->wflag = 1;
            break;
        case FS_FAT32:
            res = move_window(fs, fs->fatbase + (clst / (SS_VAL / 4)));
            if (res != FatResult::OK) break;
            val = (val & 0x0FFFFFFF) | (ld_32(fs->win + clst * 4 % SS_VAL) & 0xF0000000);
            st_32(fs->win + clst * 4 % SS_VAL, val);
            fs->wflag = 1;
            break;
        default:
            res = FatResult::INT_ERR;
            break;
        }
    }
    return res;
}

// ============================================================================
// Cluster chain management
// ============================================================================

FatResult FatFs::remove_chain(FatObjId* obj, uint32_t clst, uint32_t pclst) noexcept {
    FatResult res = FatResult::OK;
    FatFsVolume* fs = obj->fs;
    uint32_t nxt;

    if (clst < 2 || clst >= fs->n_fatent) return FatResult::INT_ERR;

    // Mark the previous cluster as end of chain
    if (pclst != 0) {
        res = put_fat(fs, pclst, 0xFFFFFFFF);
        if (res != FatResult::OK) return res;
    }

    do {
        nxt = get_fat(obj, clst);
        if (nxt == 0) break;
        if (nxt == 1) return FatResult::INT_ERR;
        if (nxt == 0xFFFFFFFF) return FatResult::DISK_ERR;
        res = put_fat(fs, clst, 0);
        if (res != FatResult::OK) return res;
        if (fs->free_clst < fs->n_fatent - 2) {
            fs->free_clst++;
            fs->fsi_flag |= 1;
        }
        clst = nxt;
    } while (clst < fs->n_fatent);
    return res;
}

uint32_t FatFs::create_chain(FatObjId* obj, uint32_t clst) noexcept {
    uint32_t cs, ncl, scl;
    FatResult res;
    FatFsVolume* fs = obj->fs;

    if (clst == 0) {
        // Create new chain
        scl = fs->last_clst;
        if (scl == 0 || scl >= fs->n_fatent) scl = 1;
    } else {
        // Stretch existing chain
        cs = get_fat(obj, clst);
        if (cs < 2) return 1;
        if (cs == 0xFFFFFFFF) return cs;
        if (cs < fs->n_fatent) return cs; // Already followed
        scl = clst;
    }

    // Find a free cluster
    if (fs->free_clst == 0) return 0; // No free cluster

    ncl = 0;
    if (scl == clst) {
        ncl = scl + 1;
        if (ncl >= fs->n_fatent) ncl = 2;
        cs = get_fat(obj, ncl);
        if (cs == 1 || cs == 0xFFFFFFFF) return cs;
        if (cs != 0) {
            cs = fs->last_clst;
            if (cs >= 2 && cs < fs->n_fatent) ncl = cs;
            ncl = 0;
        }
    }
    if (ncl == 0) {
        ncl = scl;
        for (;;) {
            ncl++;
            if (ncl >= fs->n_fatent) {
                ncl = 2;
                if (ncl > scl) return 0; // No free cluster
            }
            cs = get_fat(obj, ncl);
            if (cs == 0) break;
            if (cs == 1 || cs == 0xFFFFFFFF) return cs;
            if (ncl == scl) return 0; // No free cluster
        }
    }

    // Set the new cluster as end of chain
    res = put_fat(fs, ncl, 0xFFFFFFFF);
    if (res == FatResult::OK && clst != 0) {
        res = put_fat(fs, clst, ncl); // Link to previous
    }

    if (res == FatResult::OK) {
        fs->last_clst = ncl;
        if (fs->free_clst <= fs->n_fatent - 2) fs->free_clst--;
        fs->fsi_flag |= 1;
    } else {
        ncl = (res == FatResult::DISK_ERR) ? 0xFFFFFFFF : 1;
    }

    return ncl;
}

// ============================================================================
// Clear cluster (fill with zero)
// ============================================================================

FatResult FatFs::dir_clear(FatFsVolume* fs, uint32_t clst) noexcept {
    LBA_t sect;
    uint32_t n;

    sect = clst2sect(fs, clst);
    if (sect == 0) return FatResult::INT_ERR;
    n = fs->csize;

    if (move_window(fs, 0) != FatResult::OK) return FatResult::DISK_ERR;
    memset(fs->win, 0, SS_VAL);
    fs->winsect = sect;
    do {
        fs->wflag = 1;
        if (sync_window(fs) != FatResult::OK) return FatResult::DISK_ERR;
        sect++;
        if (--n > 0) fs->winsect = sect;
    } while (n);
    return FatResult::OK;
}

// ============================================================================
// Directory handling — cluster load
// ============================================================================

uint32_t FatFs::ld_clust(FatFsVolume* fs, const uint8_t* dir) noexcept {
    uint32_t cl = ld_16(dir + DIR_FstClusLO);
    if (fs->fs_type == FS_FAT32) {
        cl |= static_cast<uint32_t>(ld_16(dir + DIR_FstClusHI)) << 16;
    }
    return cl;
}

void FatFs::st_clust(FatFsVolume* fs, uint8_t* dir, uint32_t cl) noexcept {
    st_16(dir + DIR_FstClusLO, static_cast<uint16_t>(cl));
    if (fs->fs_type == FS_FAT32) {
        st_16(dir + DIR_FstClusHI, static_cast<uint16_t>(cl >> 16));
    }
}

// ============================================================================
// Directory index management
// ============================================================================

FatResult FatFs::dir_sdi(FatDir* dp, uint32_t ofs) noexcept {
    uint32_t csz, clst;
    FatFsVolume* fs = dp->obj.fs;

    if (ofs >= static_cast<uint32_t>((fs->fs_type == FS_FAT32) ? MAX_FAT32 : MAX_FAT16) * SZDIRE) {
        return FatResult::INT_ERR;
    }
    dp->dptr = ofs;
    clst = dp->obj.sclust;
    if (clst == 0 && fs->fs_type == FS_FAT32) {
        clst = static_cast<uint32_t>(fs->dirbase);
    }
    if (clst == 0) {
        // Static root directory (FAT12/16)
        if (ofs / SZDIRE >= fs->n_rootdir) return FatResult::INT_ERR;
        dp->sect = fs->dirbase;
    } else {
        // Dynamic table (sub-directory or FAT32 root)
        csz = static_cast<uint32_t>(fs->csize) * SS_VAL;
        while (ofs >= csz) {
            clst = get_fat(&dp->obj, clst);
            if (clst == 0xFFFFFFFF) return FatResult::DISK_ERR;
            if (clst < 2 || clst >= fs->n_fatent) return FatResult::INT_ERR;
            ofs -= csz;
        }
        dp->sect = clst2sect(fs, clst);
    }
    dp->clust = clst;
    if (dp->sect == 0) return FatResult::INT_ERR;
    dp->sect += ofs / SS_VAL;
    dp->dir = fs->win + (ofs % SS_VAL);
    return FatResult::OK;
}

FatResult FatFs::dir_next(FatDir* dp, int stretch) noexcept {
    uint32_t ofs, clst;
    FatFsVolume* fs = dp->obj.fs;

    ofs = dp->dptr + SZDIRE;
    if (ofs >= static_cast<uint32_t>((fs->fs_type == FS_FAT32) ? MAX_FAT32 : MAX_FAT16) * SZDIRE) {
        dp->sect = 0;
        return FatResult::NO_FILE;
    }
    if ((ofs % SS_VAL) == 0) {
        dp->sect++;
        if (dp->clust == 0) {
            // Static root directory
            if (ofs / SZDIRE >= fs->n_rootdir) {
                dp->sect = 0;
                return FatResult::NO_FILE;
            }
        } else {
            // Dynamic table
            if ((ofs / SS_VAL & (fs->csize - 1)) == 0) {
                clst = get_fat(&dp->obj, dp->clust);
                if (clst <= 1) return FatResult::INT_ERR;
                if (clst == 0xFFFFFFFF) return FatResult::DISK_ERR;
                if (clst >= fs->n_fatent) {
                    if (!stretch) {
                        dp->sect = 0;
                        return FatResult::NO_FILE;
                    }
                    clst = create_chain(&dp->obj, dp->clust);
                    if (clst == 0) return FatResult::DENIED;
                    if (clst == 1) return FatResult::INT_ERR;
                    if (clst == 0xFFFFFFFF) return FatResult::DISK_ERR;
                    if (dir_clear(fs, clst) != FatResult::OK) return FatResult::DISK_ERR;
                }
                dp->clust = clst;
                dp->sect = clst2sect(fs, clst);
            }
        }
    }
    dp->dptr = ofs;
    dp->dir = fs->win + ofs % SS_VAL;
    return FatResult::OK;
}

FatResult FatFs::dir_alloc(FatDir* dp, uint32_t n_ent) noexcept {
    FatResult res;
    uint32_t n;
    FatFsVolume* fs = dp->obj.fs;

    res = dir_sdi(dp, 0);
    if (res == FatResult::OK) {
        n = 0;
        do {
            res = move_window(fs, dp->sect);
            if (res != FatResult::OK) break;
            if (dp->dir[DIR_Name] == DDEM || dp->dir[DIR_Name] == 0) {
                if (++n == n_ent) break;
            } else {
                n = 0;
            }
            res = dir_next(dp, 1);
        } while (res == FatResult::OK);
    }
    if (res == FatResult::NO_FILE) res = FatResult::DENIED;
    return res;
}

// ============================================================================
// LFN helpers (FF_USE_LFN == 1)
// ============================================================================

// LFN character offsets within a 32-byte LFN entry
static constexpr uint32_t LfnOfs[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

int FatFs::cmp_lfn(const uint16_t* lfnbuf, uint8_t* dir) noexcept {
    uint32_t ni, di;
    uint16_t pchr, chr;

    if (ld_16(dir + LDIR_FstClusLO) != 0) return 0;

    ni = (static_cast<uint32_t>(dir[LDIR_Ord] & 0x3F) - 1) * 13;

    for (pchr = 1, di = 0; di < 13; di++) {
        chr = ld_16(dir + LfnOfs[di]);
        if (pchr != 0) {
            if (ni >= static_cast<uint32_t>(config::MAX_LFN + 1) ||
                ff_wtoupper(chr) != ff_wtoupper(lfnbuf[ni++])) {
                return 0;
            }
            pchr = chr;
        } else {
            if (chr != 0xFFFF) return 0;
        }
    }

    if ((dir[LDIR_Ord] & LLEF) && pchr && lfnbuf[ni]) return 0;

    return 1;
}

int FatFs::pick_lfn(uint16_t* lfnbuf, uint8_t* dir) noexcept {
    uint32_t ni, di;
    uint16_t pchr, chr;

    if (ld_16(dir + LDIR_FstClusLO) != 0) return 0;  // Check LDIR_FstClusLO is 0

    ni = (static_cast<uint32_t>(dir[LDIR_Ord] & ~LLEF) - 1) * 13;  // Offset in the name buffer

    for (pchr = 1, di = 0; di < 13; di++) {  // Process all characters in the entry
        chr = ld_16(dir + LfnOfs[di]);       // Pick a character from the entry
        if (pchr != 0) {
            if (ni >= static_cast<uint32_t>(config::MAX_LFN + 1)) return 0;  // Buffer overflow?
            lfnbuf[ni++] = pchr = chr;  // Store it
        } else {
            if (chr != 0xFFFF) return 0;  // Check filler
        }
    }

    if ((dir[LDIR_Ord] & LLEF) && pchr != 0) {  // Put terminator if last LFN part and not terminated
        if (ni >= static_cast<uint32_t>(config::MAX_LFN + 1)) return 0;  // Buffer overflow?
        lfnbuf[ni] = 0;
    }

    return 1;
}

void FatFs::put_lfn(const uint16_t* lfn, uint8_t* dir, uint8_t ord, uint8_t sum) noexcept {
    uint32_t ni, di;
    uint16_t chr;

    dir[LDIR_Chksum] = sum;       // Set checksum
    dir[LDIR_Attr] = AM_LFN;      // Set attribute
    dir[LDIR_Type] = 0;
    st_16(dir + LDIR_FstClusLO, 0);

    ni = (static_cast<uint32_t>(ord) - 1) * 13;  // Offset in the name
    di = 0;
    chr = 0;
    do {  // Fill the directory entry
        if (chr != 0xFFFF) chr = lfn[ni++];  // Get an effective character
        st_16(dir + LfnOfs[di], chr);        // Set it
        if (chr == 0) chr = 0xFFFF;          // Padding characters after the terminator
    } while (++di < 13);
    if (chr == 0xFFFF || !lfn[ni]) ord |= LLEF;  // Last LFN part is the start of an entry set
    dir[LDIR_Ord] = ord;                          // Set order in the entry set
}

void FatFs::gen_numname(uint8_t* dst, const uint8_t* src, const uint16_t* lfn, uint32_t seq) noexcept {
    uint8_t ns[8], c;
    uint32_t i, j;

    memcpy(dst, src, 11);

    if (seq > 5) {
        uint16_t wc;
        uint32_t crc_sreg = seq;
        const uint16_t* p = lfn;
        while (*p) {
            wc = *p++;
            for (i = 0; i < 16; i++) {
                crc_sreg = (crc_sreg << 1) + (wc & 1);
                wc >>= 1;
                if (crc_sreg & 0x10000) crc_sreg ^= 0x11021;
            }
        }
        seq = static_cast<uint32_t>(static_cast<uint16_t>(crc_sreg));
    }

    // Make suffix (~ + hexadecimal)
    i = 7;
    do {
        c = static_cast<uint8_t>((seq % 16) + '0');
        seq /= 16;
        if (c > '9') c += 7;
        ns[i--] = c;
    } while (i && seq);
    ns[i] = '~';

    // Append the suffix to the SFN body
    for (j = 0; j < i && dst[j] != ' '; j++) {
        if (dbc_1st(dst[j])) {
            if (j == i - 1) break;
            j++;
        }
    }
    do {
        dst[j++] = (i < 8) ? ns[i++] : ' ';
    } while (j < 8);
}

uint8_t FatFs::sum_sfn(const uint8_t* dir) noexcept {
    uint8_t sum = 0;
    uint32_t n = 11;
    do {
        sum = (sum >> 1) + (sum << 7) + *dir++;
    } while (--n);
    return sum;
}

// ============================================================================
// Directory read
// ============================================================================

FatResult FatFs::dir_read(FatDir* dp, int vol) noexcept {
    FatResult res = FatResult::NO_FILE;
    FatFsVolume* fs = dp->obj.fs;
    uint8_t attr, et;
    uint8_t ord = 0xFF, sum = 0xFF;

    while (dp->sect) {
        res = move_window(fs, dp->sect);
        if (res != FatResult::OK) break;
        et = dp->dir[DIR_Name];
        if (et == 0) {
            res = FatResult::NO_FILE;
            break;
        }
        // On the FAT/FAT32 volume
        dp->obj.attr = attr = dp->dir[DIR_Attr] & AM_MASK;
        // LFN configuration
        if (et == DDEM || et == '.' ||
            (static_cast<int>((attr & ~AM_ARC) == AM_VOL) != vol)) {
            ord = 0xFF;
        } else {
            if (attr == AM_LFN) {
                if (et & LLEF) {
                    sum = dp->dir[LDIR_Chksum];
                    et &= static_cast<uint8_t>(~LLEF);
                    ord = et;
                    dp->blk_ofs = dp->dptr;
                }
                ord = (et == ord && sum == dp->dir[LDIR_Chksum] && pick_lfn(fs->lfnbuf, dp->dir))
                          ? ord - 1
                          : 0xFF;
            } else {
                if (ord != 0 || sum != sum_sfn(dp->dir)) {
                    dp->blk_ofs = 0xFFFFFFFF;
                }
                break;
            }
        }
        res = dir_next(dp, 0);
        if (res != FatResult::OK) break;
    }

    if (res != FatResult::OK) dp->sect = 0;
    return res;
}

// ============================================================================
// Directory find
// ============================================================================

FatResult FatFs::dir_find(FatDir* dp) noexcept {
    FatResult res;
    FatFsVolume* fs = dp->obj.fs;
    uint8_t et;
    uint8_t attr, ord, sum;

    res = dir_sdi(dp, 0);
    if (res != FatResult::OK) return res;

    // On the FAT/FAT32 volume
    ord = sum = 0xFF;
    dp->blk_ofs = 0xFFFFFFFF;
    do {
        res = move_window(fs, dp->sect);
        if (res != FatResult::OK) break;
        et = dp->dir[DIR_Name];
        if (et == 0) {
            res = FatResult::NO_FILE;
            break;
        }
        // LFN configuration
        dp->obj.attr = attr = dp->dir[DIR_Attr] & AM_MASK;
        if (et == DDEM || ((attr & AM_VOL) && attr != AM_LFN)) {
            ord = 0xFF;
            dp->blk_ofs = 0xFFFFFFFF;
        } else {
            if (attr == AM_LFN) {
                if (!(dp->fn[NSFLAG] & NS_NOLFN)) {
                    if (et & LLEF) {
                        et &= static_cast<uint8_t>(~LLEF);
                        ord = et;
                        dp->blk_ofs = dp->dptr;
                        sum = dp->dir[LDIR_Chksum];
                    }
                    ord = (et == ord && sum == dp->dir[LDIR_Chksum] && cmp_lfn(fs->lfnbuf, dp->dir))
                              ? ord - 1
                              : 0xFF;
                }
            } else {
                if (ord == 0 && sum == sum_sfn(dp->dir)) break;
                if (!(dp->fn[NSFLAG] & NS_LOSS) && !memcmp(dp->dir, dp->fn, 11)) break;
                ord = 0xFF;
                dp->blk_ofs = 0xFFFFFFFF;
            }
        }
        res = dir_next(dp, 0);
    } while (res == FatResult::OK);

    return res;
}

// ============================================================================
// Directory register
// ============================================================================

FatResult FatFs::dir_register(FatDir* dp) noexcept {
    FatResult res;
    FatFsVolume* fs = dp->obj.fs;
    uint32_t n, len, n_ent;
    uint8_t sn[12];

    if (dp->fn[NSFLAG] & (NS_DOT | NS_NONAME)) return FatResult::INVALID_NAME;
    for (len = 0; fs->lfnbuf[len]; len++)
        ;

    // On the FAT/FAT32 volume
    memcpy(sn, dp->fn, 12);
    if (sn[NSFLAG] & NS_LOSS) {
        dp->fn[NSFLAG] = NS_NOLFN;
        for (n = 1; n < 100; n++) {
            gen_numname(dp->fn, sn, fs->lfnbuf, n);
            res = dir_find(dp);
            if (res != FatResult::OK) break;
        }
        if (n == 100) return FatResult::DENIED;
        if (res != FatResult::NO_FILE) return res;
        dp->fn[NSFLAG] = sn[NSFLAG];
    }

    // Create an SFN with/without LFNs
    n_ent = (sn[NSFLAG] & NS_LFN) ? (len + 12) / 13 + 1 : 1;
    res = dir_alloc(dp, n_ent);
    if (res == FatResult::OK && --n_ent) {
        res = dir_sdi(dp, dp->dptr - n_ent * SZDIRE);
        if (res == FatResult::OK) {
            uint8_t sfn_sum = sum_sfn(dp->fn);
            do {
                res = move_window(fs, dp->sect);
                if (res != FatResult::OK) break;
                put_lfn(fs->lfnbuf, dp->dir, static_cast<uint8_t>(n_ent), sfn_sum);
                fs->wflag = 1;
                res = dir_next(dp, 0);
            } while (res == FatResult::OK && --n_ent);
        }
    }

    // Set SFN entry
    if (res == FatResult::OK) {
        res = move_window(fs, dp->sect);
        if (res == FatResult::OK) {
            memset(dp->dir, 0, SZDIRE);
            memcpy(dp->dir + DIR_Name, dp->fn, 11);
            dp->dir[DIR_NTres] = dp->fn[NSFLAG] & (NS_BODY | NS_EXT);
            fs->wflag = 1;
        }
    }

    return res;
}

// ============================================================================
// Directory remove
// ============================================================================

FatResult FatFs::dir_remove(FatDir* dp) noexcept {
    FatResult res;
    FatFsVolume* fs = dp->obj.fs;
    uint32_t last = dp->dptr;

    res = (dp->blk_ofs == 0xFFFFFFFF) ? FatResult::OK : dir_sdi(dp, dp->blk_ofs);
    if (res == FatResult::OK) {
        do {
            res = move_window(fs, dp->sect);
            if (res != FatResult::OK) break;
            dp->dir[DIR_Name] = DDEM;
            fs->wflag = 1;
            if (dp->dptr >= last) break;
            res = dir_next(dp, 0);
        } while (res == FatResult::OK);
        if (res == FatResult::NO_FILE) res = FatResult::INT_ERR;
    }

    return res;
}

// ============================================================================
// Get file information from directory entry
// ============================================================================

void FatFs::get_fileinfo(FatDir* dp, FatFileInfo* fno) noexcept {
    uint32_t si, di;
    uint16_t wc, hs;
    FatFsVolume* fs = dp->obj.fs;
    uint32_t nw;

    fno->fname[0] = 0;
    if (dp->sect == 0) return;

    // LFN configuration — FAT/FAT32 volume
    if (dp->blk_ofs != 0xFFFFFFFF) {
        si = di = 0;
        hs = 0;
        while (fs->lfnbuf[si] != 0) {
            wc = fs->lfnbuf[si++];
            if (hs == 0 && (wc >= 0xD800 && wc <= 0xDFFF)) {
                hs = wc;
                continue;
            }
            nw = put_utf(static_cast<uint32_t>(hs) << 16 | wc, &fno->fname[di], config::LFN_BUF - di);
            if (nw == 0) {
                di = 0;
                break;
            }
            di += nw;
            hs = 0;
        }
        if (hs != 0) di = 0;
        fno->fname[di] = 0;
    }

    si = di = 0;
    while (si < 11) {
        wc = dp->dir[si++];
        if (wc == ' ') continue;
        if (wc == RDDEM) wc = DDEM;
        if (si == 9 && di < static_cast<uint32_t>(config::SFN_BUF)) fno->altname[di++] = '.';
        // ANSI/OEM output
        fno->altname[di++] = static_cast<char>(wc);
    }
    fno->altname[di] = 0;

    if (!fno->fname[0]) {
        if (di == 0) {
            fno->fname[di++] = '?';
        } else {
            uint8_t lcflg = NS_BODY;
            for (si = di = 0; fno->altname[si]; si++, di++) {
                wc = static_cast<uint16_t>(static_cast<uint8_t>(fno->altname[si]));
                if (wc == '.') lcflg = NS_EXT;
                if (IsUpper(wc) && (dp->dir[DIR_NTres] & lcflg)) wc += 0x20;
                fno->fname[di] = static_cast<char>(wc);
            }
        }
        fno->fname[di] = 0;
        if (!dp->dir[DIR_NTres]) fno->altname[0] = 0;
    }

    fno->fattrib = dp->dir[DIR_Attr] & AM_MASK;
    fno->fsize = ld_32(dp->dir + DIR_FileSize);
    fno->ftime = ld_16(dp->dir + DIR_ModTime + 0);
    fno->fdate = ld_16(dp->dir + DIR_ModTime + 2);
}

// ============================================================================
// Create name (parse path segment and create SFN/LFN)
// ============================================================================

FatResult FatFs::create_name(FatDir* dp, const char** path) noexcept {
    // LFN configuration
    uint8_t b, cf;
    uint16_t wc;
    uint16_t* lfn;
    const char* p;
    uint32_t uc;
    uint32_t i, ni, si, di;

    // Create an LFN into LFN working buffer
    p = *path;
    lfn = dp->obj.fs->lfnbuf;
    di = 0;
    for (;;) {
        uc = tchar2uni(&p);
        if (uc == 0xFFFFFFFF) return FatResult::INVALID_NAME;
        if (uc >= 0x10000) lfn[di++] = static_cast<uint16_t>(uc >> 16);
        wc = static_cast<uint16_t>(uc);
        if (wc < ' ' || IsSeparator(wc)) break;
        if (wc < 0x80 && strchr("*:<>|\"\?\x7F", static_cast<int>(wc))) return FatResult::INVALID_NAME;
        if (di >= static_cast<uint32_t>(config::MAX_LFN)) return FatResult::INVALID_NAME;
        lfn[di++] = wc;
    }
    if (wc < ' ') {
        cf = NS_LAST;
    } else {
        while (IsSeparator(*p)) p++;
        cf = 0;
        if (IsTerminator(*p)) cf = NS_LAST;
    }
    *path = p;

    // No FF_FS_RPATH: skip dot name handling

    while (di) {
        wc = lfn[di - 1];
        if (wc != ' ' && wc != '.') break;
        di--;
    }
    lfn[di] = 0;
    if (di == 0) return FatResult::INVALID_NAME;

    // Create SFN in directory form
    for (si = 0; lfn[si] == ' '; si++)
        ;
    if (si > 0 || lfn[si] == '.') cf |= NS_LOSS | NS_LFN;
    while (di > 0 && lfn[di - 1] != '.') di--;

    memset(dp->fn, ' ', 11);
    i = b = 0;
    ni = 8;
    for (;;) {
        wc = lfn[si++];
        if (wc == 0) break;
        if (wc == ' ' || (wc == '.' && si != di)) {
            cf |= NS_LOSS | NS_LFN;
            continue;
        }

        if (i >= ni || si == di) {
            if (ni == 11) {
                cf |= NS_LOSS | NS_LFN;
                break;
            }
            if (si != di) cf |= NS_LOSS | NS_LFN;
            if (si > di) break;
            si = di;
            i = 8;
            ni = 11;
            b <<= 2;
            continue;
        }

        if (wc >= 0x80) {
            cf |= NS_LFN;
            // SBCS code page (CP437 < 900)
            wc = ff_uni2oem(wc, CODEPAGE);
            if (wc & 0x80) wc = ExCvt[wc & 0x7F];
        }

        if (wc >= 0x100) {
            if (i >= ni - 1) {
                cf |= NS_LOSS | NS_LFN;
                i = ni;
                continue;
            }
            dp->fn[i++] = static_cast<uint8_t>(wc >> 8);
        } else {
            if (wc == 0 || strchr("+,;=[]", static_cast<int>(wc))) {
                wc = '_';
                cf |= NS_LOSS | NS_LFN;
            } else {
                if (IsUpper(wc)) {
                    b |= 2;
                }
                if (IsLower(wc)) {
                    b |= 1;
                    wc -= 0x20;
                }
            }
        }
        dp->fn[i++] = static_cast<uint8_t>(wc);
    }

    if (dp->fn[0] == DDEM) dp->fn[0] = RDDEM;

    if (ni == 8) b <<= 2;
    if ((b & 0x0C) == 0x0C || (b & 0x03) == 0x03) cf |= NS_LFN;
    if (!(cf & NS_LFN)) {
        if (b & 0x01) cf |= NS_EXT;
        if (b & 0x04) cf |= NS_BODY;
    }

    dp->fn[NSFLAG] = cf;

    return FatResult::OK;
}

// ============================================================================
// Follow path
// ============================================================================

FatResult FatFs::follow_path(FatDir* dp, const char* path) noexcept {
    FatResult res;
    uint8_t ns;
    FatFsVolume* fs = dp->obj.fs;

    // No FF_FS_RPATH: always start from root
    while (IsSeparator(*path)) path++;
    dp->obj.sclust = 0;

    if (static_cast<uint32_t>(static_cast<uint8_t>(*path)) < ' ') {
        dp->fn[NSFLAG] = NS_NONAME;
        res = dir_sdi(dp, 0);
    } else {
        for (;;) {
            res = create_name(dp, &path);
            if (res != FatResult::OK) break;
            ns = dp->fn[NSFLAG];
            res = dir_find(dp);
            if (res != FatResult::OK) {
                if (res == FatResult::NO_FILE) {
                    if (!(ns & NS_LAST)) res = FatResult::NO_PATH;
                }
                break;
            }
            if (ns & NS_LAST) break;
            if (!(dp->obj.attr & AM_DIR)) {
                res = FatResult::NO_PATH;
                break;
            }
            dp->obj.sclust = ld_clust(fs, fs->win + dp->dptr % SS_VAL);
        }
    }

    return res;
}

// ============================================================================
// Get logical drive number from path
// ============================================================================

static int get_ldnumber(const char** path) {
    const char* tp;
    const char* tt;
    char chr;

    tt = tp = *path;
    if (!tp) return -1;
    do {
        chr = *tt++;
    } while (!IsTerminator(chr) && chr != ':');

    if (chr == ':') {
        int i = config::VOLUMES;
        if (IsDigit(*tp) && tp + 2 == tt) {
            i = static_cast<int>(*tp - '0');
        }
        if (i >= config::VOLUMES) return -1;
        *path = tt;
        return i;
    }
    // No FF_FS_RPATH: default drive is 0
    return 0;
}

// ============================================================================
// Check if a sector is FAT VBR
// ============================================================================

// Returns 0:FAT/FAT32 VBR, 2:Not FAT and valid BS, 3:Not FAT and invalid BS, 4:Disk error
uint32_t FatFs::check_fs(FatFsVolume* fs, LBA_t sect) noexcept {
    uint16_t w, sign;
    uint8_t b;

    fs->wflag = 0;
    fs->winsect = static_cast<LBA_t>(0) - 1;
    if (move_window(fs, sect) != FatResult::OK) return 4;
    sign = ld_16(fs->win + BS_55AA);
    // No exFAT check (FF_FS_EXFAT=0)
    b = fs->win[BS_JmpBoot];
    if (b == 0xEB || b == 0xE9 || b == 0xE8) {
        if (sign == 0xAA55 && !memcmp(fs->win + BS_FilSysType32, "FAT32   ", 8)) {
            return 0;
        }
        w = ld_16(fs->win + BPB_BytsPerSec);
        b = fs->win[BPB_SecPerClus];
        if ((w & (w - 1)) == 0 && w >= config::MIN_SS && w <= config::MAX_SS
            && b != 0 && (b & (b - 1)) == 0
            && ld_16(fs->win + BPB_RsvdSecCnt) != 0
            && static_cast<uint32_t>(fs->win[BPB_NumFATs]) - 1 <= 1
            && ld_16(fs->win + BPB_RootEntCnt) != 0
            && (ld_16(fs->win + BPB_TotSec16) >= 128 || ld_32(fs->win + BPB_TotSec32) >= 0x10000)
            && ld_16(fs->win + BPB_FATSz16) != 0) {
            return 0;
        }
    }
    return sign == 0xAA55 ? 2 : 3;
}

// ============================================================================
// Find volume (partition search)
// Returns 0:FAT, 2:not FAT valid BS, 3:not FAT invalid BS, 4:disk error
// ============================================================================

uint32_t FatFs::find_volume(FatFsVolume* fs, uint32_t part) noexcept {
    uint32_t fmt, i;
    uint32_t mbr_pt[4];

    fmt = check_fs(fs, 0);  // Load sector 0 and check if it is an FAT VBR as SFD format
    if (fmt != 2 && (fmt >= 3 || part == 0)) return fmt;

    // Sector 0 is not an FAT VBR or forced partition number wants a partitioned drive
    if (part > 4) return 3;
    for (i = 0; i < 4; i++) {
        mbr_pt[i] = ld_32(fs->win + MBR_Table + static_cast<size_t>(i) * SZ_PTE + PTE_StLba);
    }
    i = part ? part - 1 : 0;
    do {
        fmt = mbr_pt[i] ? check_fs(fs, mbr_pt[i]) : 3;
    } while (part == 0 && fmt >= 2 && ++i < 4);
    return fmt;
}

// ============================================================================
// Mount volume
// ============================================================================

FatResult FatFs::mount_volume(const char** path, FatFsVolume** rfs, uint8_t mode) noexcept {
    int vol;
    FatFsVolume* fs;
    uint8_t stat;
    LBA_t bsect;
    uint32_t fmt;

    *rfs = nullptr;
    vol = get_ldnumber(path);
    if (vol < 0) return FatResult::INVALID_DRIVE;

    fs = fat_fs[vol];
    if (!fs) return FatResult::NOT_ENABLED;
    *rfs = fs;

    mode &= static_cast<uint8_t>(~FA_READ);
    if (fs->fs_type != 0) {
        stat = disk_status(fs->pdrv);
        if (!(stat & STA_NOINIT)) {
            if (mode && (stat & STA_PROTECT)) {
                return FatResult::WRITE_PROTECTED;
            }
            return FatResult::OK;
        }
    }

    // Volume is not valid — attempt to mount
    fs->fs_type = 0;
    stat = disk_initialize(fs->pdrv);
    if (stat & STA_NOINIT) {
        return FatResult::NOT_READY;
    }
    if (mode && (stat & STA_PROTECT)) {
        return FatResult::WRITE_PROTECTED;
    }
    // FF_MIN_SS == FF_MAX_SS: skip get sector size

    // Find FAT volume
    fmt = find_volume(fs, LD2PT(vol));
    if (fmt == 4) return FatResult::DISK_ERR;
    if (fmt >= 2) return FatResult::NO_FILESYSTEM;
    bsect = fs->winsect;

    // Initialize the filesystem object (FAT/FAT32, no exFAT)
    {
        uint32_t tsect, sysect, fasize, nclst, szbfat;
        uint16_t nrsv;

        if (ld_16(fs->win + BPB_BytsPerSec) != SS_VAL) return FatResult::NO_FILESYSTEM;

        fasize = ld_16(fs->win + BPB_FATSz16);
        if (fasize == 0) fasize = ld_32(fs->win + BPB_FATSz32);
        fs->fsize = fasize;

        fs->n_fats = fs->win[BPB_NumFATs];
        if (fs->n_fats != 1 && fs->n_fats != 2) return FatResult::NO_FILESYSTEM;
        fasize *= fs->n_fats;

        fs->csize = fs->win[BPB_SecPerClus];
        if (fs->csize == 0 || (fs->csize & (fs->csize - 1))) return FatResult::NO_FILESYSTEM;

        fs->n_rootdir = ld_16(fs->win + BPB_RootEntCnt);
        if (fs->n_rootdir % (SS_VAL / SZDIRE)) return FatResult::NO_FILESYSTEM;

        tsect = ld_16(fs->win + BPB_TotSec16);
        if (tsect == 0) tsect = ld_32(fs->win + BPB_TotSec32);

        nrsv = ld_16(fs->win + BPB_RsvdSecCnt);
        if (nrsv == 0) return FatResult::NO_FILESYSTEM;

        // Determine FAT sub-type
        sysect = nrsv + fasize + fs->n_rootdir / (SS_VAL / SZDIRE);
        if (tsect < sysect) return FatResult::NO_FILESYSTEM;
        nclst = (tsect - sysect) / fs->csize;
        if (nclst == 0) return FatResult::NO_FILESYSTEM;
        fmt = 0;
        if (nclst <= MAX_FAT32) fmt = FS_FAT32;
        if (nclst <= MAX_FAT16) fmt = FS_FAT16;
        if (nclst <= MAX_FAT12) fmt = FS_FAT12;
        if (fmt == 0) return FatResult::NO_FILESYSTEM;

        // Boundaries
        fs->n_fatent = nclst + 2;
        fs->volbase = bsect;
        fs->fatbase = bsect + nrsv;
        fs->database = bsect + sysect;
        if (fmt == FS_FAT32) {
            if (ld_16(fs->win + BPB_FSVer32) != 0) return FatResult::NO_FILESYSTEM;
            if (fs->n_rootdir != 0) return FatResult::NO_FILESYSTEM;
            fs->dirbase = ld_32(fs->win + BPB_RootClus32);
            szbfat = fs->n_fatent * 4;
        } else {
            if (fs->n_rootdir == 0) return FatResult::NO_FILESYSTEM;
            fs->dirbase = fs->fatbase + fasize;
            szbfat = (fmt == FS_FAT16) ? fs->n_fatent * 2
                                       : fs->n_fatent * 3 / 2 + (fs->n_fatent & 1);
        }
        if (fs->fsize < (szbfat + (SS_VAL - 1)) / SS_VAL) return FatResult::NO_FILESYSTEM;

        // Get FSInfo if available
        fs->last_clst = fs->free_clst = 0xFFFFFFFF;
        fs->fsi_flag = 0x80;
        if (fmt == FS_FAT32 && ld_16(fs->win + BPB_FSInfo32) == 1 &&
            move_window(fs, bsect + 1) == FatResult::OK) {
            fs->fsi_flag = 0;
            if (ld_32(fs->win + FSI_LeadSig) == 0x41615252 &&
                ld_32(fs->win + FSI_StrucSig) == 0x61417272 &&
                ld_32(fs->win + FSI_TrailSig) == 0xAA550000) {
                // FF_FS_NOFSINFO == 0: trust both
                fs->free_clst = ld_32(fs->win + FSI_Free_Count);
                fs->last_clst = ld_32(fs->win + FSI_Nxt_Free);
            }
        }
    }

    fs->fs_type = static_cast<uint8_t>(fmt);
    fs->id = ++Fsid;

    // Set LFN buffer pointer (static BSS, FF_USE_LFN == 1)
    fs->lfnbuf = lfn_buf;

    return FatResult::OK;
}

// ============================================================================
// Validate object
// ============================================================================

FatResult FatFs::validate(FatObjId* obj, FatFsVolume** rfs) noexcept {
    FatResult res = FatResult::INVALID_OBJECT;

    if (obj && obj->fs && obj->fs->fs_type && obj->id == obj->fs->id) {
        if (!(disk_status(obj->fs->pdrv) & STA_NOINIT)) {
            res = FatResult::OK;
        }
    }
    *rfs = (res == FatResult::OK) ? obj->fs : nullptr;
    return res;
}

// ============================================================================
// Public API: mount
// ============================================================================

FatResult FatFs::mount(FatFsVolume* fs, const char* path, uint8_t opt) noexcept {
    FatFsVolume* cfs;
    int vol;
    FatResult res;
    const char* rp = path;

    vol = get_ldnumber(&rp);
    if (vol < 0) return FatResult::INVALID_DRIVE;

    cfs = fat_fs[vol];
    if (cfs) {
        fat_fs[vol] = nullptr;
        cfs->fs_type = 0;
    }

    if (fs) {
        fs->pdrv = LD2PD(vol);
        fs->fs_type = 0;
        fat_fs[vol] = fs;
    }

    if (opt == 0) return FatResult::OK;

    res = mount_volume(&path, &fs, 0);
    return res;
}

// ============================================================================
// Public API: unmount
// ============================================================================

FatResult FatFs::unmount(const char* path) noexcept {
    return mount(nullptr, path, 0);
}

// ============================================================================
// Public API: open
// ============================================================================

FatResult FatFs::open(FatFile* fp, const char* path, uint8_t mode) noexcept {
    FatResult res;
    FatDir dj{};
    FatFsVolume* fs;

    if (!fp) return FatResult::INVALID_OBJECT;

    mode &= (FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_CREATE_NEW | FA_OPEN_ALWAYS | FA_OPEN_APPEND);
    res = mount_volume(&path, &fs, mode);

    if (res == FatResult::OK) {
        fp->obj.fs = fs;
        dj.obj.fs = fs;
        res = follow_path(&dj, path);

        // Read/Write configuration
        if (res == FatResult::OK) {
            if (dj.fn[NSFLAG] & NS_NONAME) {
                res = FatResult::INVALID_NAME;
            }
        }
        // Create or Open a file
        if (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW)) {
            if (res != FatResult::OK) {
                if (res == FatResult::NO_FILE) {
                    res = dir_register(&dj);
                }
                mode |= FA_CREATE_ALWAYS;
            } else {
                if (mode & FA_CREATE_NEW) {
                    res = FatResult::EXIST;
                } else {
                    if (dj.obj.attr & (AM_RDO | AM_DIR)) res = FatResult::DENIED;
                }
            }
            if (res == FatResult::OK && (mode & FA_CREATE_ALWAYS)) {
                uint32_t tm = get_fattime();
                {
                    uint32_t cl;
                    st_32(dj.dir + DIR_CrtTime, tm);
                    st_32(dj.dir + DIR_ModTime, tm);
                    cl = ld_clust(fs, dj.dir);
                    dj.dir[DIR_Attr] = AM_ARC;
                    st_clust(fs, dj.dir, 0);
                    st_32(dj.dir + DIR_FileSize, 0);
                    fs->wflag = 1;
                    if (cl != 0) {
                        LBA_t sc = fs->winsect;
                        res = remove_chain(&dj.obj, cl, 0);
                        if (res == FatResult::OK) {
                            res = move_window(fs, sc);
                            fs->last_clst = cl - 1;
                        }
                    }
                }
            }
        } else {
            // Open existing file
            if (res == FatResult::OK) {
                if (dj.obj.attr & AM_DIR) {
                    res = FatResult::NO_FILE;
                } else {
                    if ((mode & FA_WRITE) && (dj.obj.attr & AM_RDO)) {
                        res = FatResult::DENIED;
                    }
                }
            }
        }
        if (res == FatResult::OK) {
            if (mode & FA_CREATE_ALWAYS) mode |= FA_MODIFIED;
            fp->dir_sect = fs->winsect;
            fp->dir_ptr = dj.dir;
        }

        if (res == FatResult::OK) {
            fp->obj.sclust = ld_clust(fs, dj.dir);
            fp->obj.objsize = ld_32(dj.dir + DIR_FileSize);
            fp->obj.id = fs->id;
            fp->flag = mode;
            fp->err = 0;
            fp->sect = 0;
            fp->fptr = 0;
            memset(fp->buf, 0, sizeof fp->buf);
            if ((mode & FA_SEEKEND) && fp->obj.objsize > 0) {
                uint32_t bcs, clst;
                FSIZE_t ofs;

                fp->fptr = fp->obj.objsize;
                bcs = static_cast<uint32_t>(fs->csize) * SS_VAL;
                clst = fp->obj.sclust;
                for (ofs = fp->obj.objsize; res == FatResult::OK && ofs > bcs; ofs -= bcs) {
                    clst = get_fat(&fp->obj, clst);
                    if (clst <= 1) res = FatResult::INT_ERR;
                    if (clst == 0xFFFFFFFF) res = FatResult::DISK_ERR;
                }
                fp->clust = clst;
                if (res == FatResult::OK && ofs % SS_VAL) {
                    LBA_t sec = clst2sect(fs, clst);
                    if (sec == 0) {
                        res = FatResult::INT_ERR;
                    } else {
                        fp->sect = sec + static_cast<uint32_t>(ofs / SS_VAL);
                        if (disk_read(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                            res = FatResult::DISK_ERR;
                        }
                    }
                }
            }
        }
    }

    if (res != FatResult::OK) fp->obj.fs = nullptr;

    return res;
}

// ============================================================================
// Public API: close
// ============================================================================

FatResult FatFs::close(FatFile* fp) noexcept {
    FatResult res;
    FatFsVolume* fs;

    res = sync(fp);
    if (res == FatResult::OK) {
        res = validate(&fp->obj, &fs);
        if (res == FatResult::OK) {
            fp->obj.fs = nullptr;
        }
    }
    return res;
}

// ============================================================================
// Public API: read
// ============================================================================

FatResult FatFs::read(FatFile* fp, void* buff, uint32_t btr, uint32_t* br) noexcept {
    FatResult res;
    FatFsVolume* fs;
    LBA_t sect;
    FSIZE_t remain;
    uint32_t rcnt, cc, csect;
    auto* rbuff = static_cast<uint8_t*>(buff);

    *br = 0;
    res = validate(&fp->obj, &fs);
    if (res != FatResult::OK || (res = static_cast<FatResult>(fp->err)) != FatResult::OK) return res;
    if (!(fp->flag & FA_READ)) return FatResult::DENIED;
    remain = fp->obj.objsize - fp->fptr;
    if (btr > remain) btr = static_cast<uint32_t>(remain);

    for (; btr > 0; btr -= rcnt, *br += rcnt, rbuff += rcnt, fp->fptr += rcnt) {
        if (fp->fptr % SS_VAL == 0) {
            csect = static_cast<uint32_t>(fp->fptr / SS_VAL & (fs->csize - 1));
            if (csect == 0) {
                uint32_t clst;
                if (fp->fptr == 0) {
                    clst = fp->obj.sclust;
                } else {
                    clst = get_fat(&fp->obj, fp->clust);
                }
                if (clst < 2) {
                    fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                    return FatResult::INT_ERR;
                }
                if (clst == 0xFFFFFFFF) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                fp->clust = clst;
            }
            sect = clst2sect(fs, fp->clust);
            if (sect == 0) {
                fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                return FatResult::INT_ERR;
            }
            sect += csect;
            cc = btr / SS_VAL;
            if (cc > 0) {
                if (csect + cc > fs->csize) {
                    cc = fs->csize - csect;
                }
                if (disk_read(fs->pdrv, rbuff, sect, cc) != DiskResult::OK) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                // Replace cached dirty sector if it overlaps
                if ((fp->flag & FA_DIRTY) && fp->sect - sect < cc) {
                    memcpy(rbuff + ((fp->sect - sect) * SS_VAL), fp->buf, SS_VAL);
                }
                rcnt = SS_VAL * cc;
                continue;
            }
            if (fp->sect != sect) {
                if (fp->flag & FA_DIRTY) {
                    if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                        fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                        return FatResult::DISK_ERR;
                    }
                    fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
                }
                if (disk_read(fs->pdrv, fp->buf, sect, 1) != DiskResult::OK) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
            }
            fp->sect = sect;
        }
        rcnt = SS_VAL - static_cast<uint32_t>(fp->fptr) % SS_VAL;
        if (rcnt > btr) rcnt = btr;
        memcpy(rbuff, fp->buf + fp->fptr % SS_VAL, rcnt);
    }

    return FatResult::OK;
}

// ============================================================================
// Public API: write
// ============================================================================

FatResult FatFs::write(FatFile* fp, const void* buff, uint32_t btw, uint32_t* bw) noexcept {
    FatResult res;
    FatFsVolume* fs;
    uint32_t clst;
    LBA_t sect;
    uint32_t wcnt, cc, csect;
    const auto* wbuff = static_cast<const uint8_t*>(buff);

    *bw = 0;
    res = validate(&fp->obj, &fs);
    if (res != FatResult::OK || (res = static_cast<FatResult>(fp->err)) != FatResult::OK) return res;
    if (!(fp->flag & FA_WRITE)) return FatResult::DENIED;

    // Check fptr wrap-around (file size cannot reach 4 GiB at FAT volume)
    if (static_cast<uint32_t>(fp->fptr + btw) < static_cast<uint32_t>(fp->fptr)) {
        btw = static_cast<uint32_t>(0xFFFFFFFF - static_cast<uint32_t>(fp->fptr));
    }

    for (; btw > 0;
         btw -= wcnt, *bw += wcnt, wbuff += wcnt, fp->fptr += wcnt,
         fp->obj.objsize = (fp->fptr > fp->obj.objsize) ? fp->fptr : fp->obj.objsize) {
        if (fp->fptr % SS_VAL == 0) {
            csect = static_cast<uint32_t>(fp->fptr / SS_VAL) & (fs->csize - 1);
            if (csect == 0) {
                if (fp->fptr == 0) {
                    clst = fp->obj.sclust;
                    if (clst == 0) {
                        clst = create_chain(&fp->obj, 0);
                    }
                } else {
                    clst = create_chain(&fp->obj, fp->clust);
                }
                if (clst == 0) break;
                if (clst == 1) {
                    fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                    return FatResult::INT_ERR;
                }
                if (clst == 0xFFFFFFFF) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                fp->clust = clst;
                if (fp->obj.sclust == 0) fp->obj.sclust = clst;
            }
            if (fp->flag & FA_DIRTY) {
                if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
            }
            sect = clst2sect(fs, fp->clust);
            if (sect == 0) {
                fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                return FatResult::INT_ERR;
            }
            sect += csect;
            cc = btw / SS_VAL;
            if (cc > 0) {
                if (csect + cc > fs->csize) {
                    cc = fs->csize - csect;
                }
                if (disk_write(fs->pdrv, wbuff, sect, cc) != DiskResult::OK) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                if (fp->sect - sect < cc) {
                    memcpy(fp->buf, wbuff + ((fp->sect - sect) * SS_VAL), SS_VAL);
                    fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
                }
                wcnt = SS_VAL * cc;
                continue;
            }
            if (fp->sect != sect && fp->fptr < fp->obj.objsize &&
                disk_read(fs->pdrv, fp->buf, sect, 1) != DiskResult::OK) {
                fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                return FatResult::DISK_ERR;
            }
            fp->sect = sect;
        }
        wcnt = SS_VAL - static_cast<uint32_t>(fp->fptr) % SS_VAL;
        if (wcnt > btw) wcnt = btw;
        memcpy(fp->buf + fp->fptr % SS_VAL, wbuff, wcnt);
        fp->flag |= FA_DIRTY;
    }

    fp->flag |= FA_MODIFIED;

    return FatResult::OK;
}

// ============================================================================
// Public API: sync
// ============================================================================

FatResult FatFs::sync(FatFile* fp) noexcept {
    FatResult res;
    FatFsVolume* fs;

    res = validate(&fp->obj, &fs);
    if (res == FatResult::OK) {
        if (fp->flag & FA_MODIFIED) {
            if (fp->flag & FA_DIRTY) {
                if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) return FatResult::DISK_ERR;
                fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
            }
            // Update the directory entry
            res = move_window(fs, fp->dir_sect);
            if (res == FatResult::OK) {
                uint8_t* dir = fp->dir_ptr;
                dir[DIR_Attr] |= AM_ARC;
                st_clust(fp->obj.fs, dir, fp->obj.sclust);
                st_32(dir + DIR_FileSize, static_cast<uint32_t>(fp->obj.objsize));
                st_32(dir + DIR_ModTime, get_fattime());
                st_16(dir + DIR_LstAccDate, 0);
                fs->wflag = 1;
                res = sync_fs(fs);
                fp->flag &= static_cast<uint8_t>(~FA_MODIFIED);
            }
        }
    }

    return res;
}

// ============================================================================
// Public API: lseek
// ============================================================================

FatResult FatFs::lseek(FatFile* fp, FSIZE_t ofs) noexcept {
    FatResult res;
    FatFsVolume* fs;
    uint32_t clst, bcs;
    LBA_t nsect;
    FSIZE_t ifptr;

    res = validate(&fp->obj, &fs);
    if (res == FatResult::OK) res = static_cast<FatResult>(fp->err);
    if (res != FatResult::OK) return res;

    // Normal Seek
    {
        if (ofs > fp->obj.objsize && !(fp->flag & FA_WRITE)) {
            ofs = fp->obj.objsize;
        }
        ifptr = fp->fptr;
        fp->fptr = nsect = 0;
        if (ofs > 0) {
            bcs = static_cast<uint32_t>(fs->csize) * SS_VAL;
            if (ifptr > 0 && (ofs - 1) / bcs >= (ifptr - 1) / bcs) {
                fp->fptr = (ifptr - 1) & ~static_cast<FSIZE_t>(bcs - 1);
                ofs -= fp->fptr;
                clst = fp->clust;
            } else {
                clst = fp->obj.sclust;
                if (clst == 0) {
                    clst = create_chain(&fp->obj, 0);
                    if (clst == 1) {
                        fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                        return FatResult::INT_ERR;
                    }
                    if (clst == 0xFFFFFFFF) {
                        fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                        return FatResult::DISK_ERR;
                    }
                    fp->obj.sclust = clst;
                }
                fp->clust = clst;
            }
            if (clst != 0) {
                while (ofs > bcs) {
                    ofs -= bcs;
                    fp->fptr += bcs;
                    if (fp->flag & FA_WRITE) {
                        clst = create_chain(&fp->obj, clst);
                        if (clst == 0) {
                            ofs = 0;
                            break;
                        }
                    } else {
                        clst = get_fat(&fp->obj, clst);
                    }
                    if (clst == 0xFFFFFFFF) {
                        fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                        return FatResult::DISK_ERR;
                    }
                    if (clst <= 1 || clst >= fs->n_fatent) {
                        fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                        return FatResult::INT_ERR;
                    }
                    fp->clust = clst;
                }
                fp->fptr += ofs;
                if (ofs % SS_VAL) {
                    nsect = clst2sect(fs, clst);
                    if (nsect == 0) {
                        fp->err = static_cast<uint8_t>(FatResult::INT_ERR);
                        return FatResult::INT_ERR;
                    }
                    nsect += static_cast<uint32_t>(ofs / SS_VAL);
                }
            }
        }
        if (fp->fptr > fp->obj.objsize) {
            fp->obj.objsize = fp->fptr;
            fp->flag |= FA_MODIFIED;
        }
        if (fp->fptr % SS_VAL && nsect != fp->sect) {
            if (fp->flag & FA_DIRTY) {
                if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                    fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                    return FatResult::DISK_ERR;
                }
                fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
            }
            if (disk_read(fs->pdrv, fp->buf, nsect, 1) != DiskResult::OK) {
                fp->err = static_cast<uint8_t>(FatResult::DISK_ERR);
                return FatResult::DISK_ERR;
            }
            fp->sect = nsect;
        }
    }

    return res;
}

// ============================================================================
// Public API: truncate
// ============================================================================

FatResult FatFs::truncate(FatFile* fp) noexcept {
    FatResult res;
    FatFsVolume* fs;
    uint32_t ncl;

    res = validate(&fp->obj, &fs);
    if (res != FatResult::OK || (res = static_cast<FatResult>(fp->err)) != FatResult::OK) return res;
    if (!(fp->flag & FA_WRITE)) return FatResult::DENIED;

    if (fp->fptr < fp->obj.objsize) {
        if (fp->fptr == 0) {
            res = remove_chain(&fp->obj, fp->obj.sclust, 0);
            fp->obj.sclust = 0;
        } else {
            ncl = get_fat(&fp->obj, fp->clust);
            res = FatResult::OK;
            if (ncl == 0xFFFFFFFF) res = FatResult::DISK_ERR;
            if (ncl == 1) res = FatResult::INT_ERR;
            if (res == FatResult::OK && ncl < fs->n_fatent) {
                res = remove_chain(&fp->obj, ncl, fp->clust);
            }
        }
        fp->obj.objsize = fp->fptr;
        fp->flag |= FA_MODIFIED;
        if (res == FatResult::OK && (fp->flag & FA_DIRTY)) {
            if (disk_write(fs->pdrv, fp->buf, fp->sect, 1) != DiskResult::OK) {
                res = FatResult::DISK_ERR;
            } else {
                fp->flag &= static_cast<uint8_t>(~FA_DIRTY);
            }
        }
        if (res != FatResult::OK) {
            fp->err = static_cast<uint8_t>(res);
            return res;
        }
    }

    return res;
}

// ============================================================================
// Public API: opendir
// ============================================================================

FatResult FatFs::opendir(FatDir* dp, const char* path) noexcept {
    FatResult res;
    FatFsVolume* fs;

    if (!dp) return FatResult::INVALID_OBJECT;

    res = mount_volume(&path, &fs, 0);
    if (res == FatResult::OK) {
        dp->obj.fs = fs;
        res = follow_path(dp, path);
        if (res == FatResult::OK) {
            if (!(dp->fn[NSFLAG] & NS_NONAME)) {
                if (dp->obj.attr & AM_DIR) {
                    dp->obj.sclust = ld_clust(fs, dp->dir);
                } else {
                    res = FatResult::NO_PATH;
                }
            }
            if (res == FatResult::OK) {
                dp->obj.id = fs->id;
                res = dir_sdi(dp, 0);
            }
        }
        if (res == FatResult::NO_FILE) res = FatResult::NO_PATH;
    }
    if (res != FatResult::OK) dp->obj.fs = nullptr;

    return res;
}

// ============================================================================
// Public API: closedir
// ============================================================================

FatResult FatFs::closedir(FatDir* dp) noexcept {
    FatResult res;
    FatFsVolume* fs;

    res = validate(&dp->obj, &fs);
    if (res == FatResult::OK) {
        dp->obj.fs = nullptr;
    }
    return res;
}

// ============================================================================
// Public API: readdir
// ============================================================================

FatResult FatFs::readdir(FatDir* dp, FatFileInfo* fno) noexcept {
    FatResult res;
    FatFsVolume* fs;

    res = validate(&dp->obj, &fs);
    if (res == FatResult::OK) {
        if (!fno) {
            res = dir_sdi(dp, 0);
        } else {
            fno->fname[0] = 0;
            res = dir_read(dp, 0);
            if (res == FatResult::NO_FILE) res = FatResult::OK;
            if (res == FatResult::OK) {
                get_fileinfo(dp, fno);
                res = dir_next(dp, 0);
                if (res == FatResult::NO_FILE) res = FatResult::OK;
            }
        }
    }

    if (fno && res != FatResult::OK) fno->fname[0] = 0;
    return res;
}

// ============================================================================
// Public API: stat
// ============================================================================

FatResult FatFs::stat(const char* path, FatFileInfo* fno) noexcept {
    FatResult res;
    FatDir dj{};

    res = mount_volume(&path, &dj.obj.fs, 0);

    if (res == FatResult::OK) {
        res = follow_path(&dj, path);
        if (res == FatResult::OK) {
            if (dj.fn[NSFLAG] & NS_NONAME) {
                res = FatResult::INVALID_NAME;
            } else {
                if (fno) get_fileinfo(&dj, fno);
            }
        }
    }

    if (fno && res != FatResult::OK) fno->fname[0] = 0;
    return res;
}

// ============================================================================
// Public API: getfree
// ============================================================================

FatResult FatFs::getfree(const char* path, uint32_t* nclst, FatFsVolume** fatfs) noexcept {
    FatResult res;
    FatFsVolume* fs;
    uint32_t nfree, clst, stat_val;
    LBA_t sect;
    uint32_t i;
    FatObjId obj{};

    res = mount_volume(&path, &fs, 0);

    if (res == FatResult::OK) {
        *fatfs = fs;
        if (fs->free_clst <= fs->n_fatent - 2) {
            *nclst = fs->free_clst;
        } else {
            nfree = 0;
            if (fs->fs_type == FS_FAT12) {
                clst = 2;
                obj.fs = fs;
                do {
                    stat_val = get_fat(&obj, clst);
                    if (stat_val == 0xFFFFFFFF) {
                        res = FatResult::DISK_ERR;
                        break;
                    }
                    if (stat_val == 1) {
                        res = FatResult::INT_ERR;
                        break;
                    }
                    if (stat_val == 0) nfree++;
                } while (++clst < fs->n_fatent);
            } else {
                // FAT16/32: Scan WORD/DWORD FAT entries
                clst = fs->n_fatent;
                sect = fs->fatbase;
                i = 0;
                do {
                    if (i == 0) {
                        res = move_window(fs, sect++);
                        if (res != FatResult::OK) break;
                    }
                    if (fs->fs_type == FS_FAT16) {
                        if (ld_16(fs->win + i) == 0) nfree++;
                        i += 2;
                    } else {
                        if ((ld_32(fs->win + i) & 0x0FFFFFFF) == 0) nfree++;
                        i += 4;
                    }
                    i %= SS_VAL;
                } while (--clst);
            }
            if (res == FatResult::OK) {
                *nclst = nfree;
                fs->free_clst = nfree;
                fs->fsi_flag |= 1;
            }
        }
    }

    return res;
}

// ============================================================================
// Public API: unlink
// ============================================================================

FatResult FatFs::unlink(const char* path) noexcept {
    FatResult res;
    FatFsVolume* fs;
    FatDir dj{}, sdj{};
    uint32_t dclst = 0;

    res = mount_volume(&path, &fs, FA_WRITE);
    if (res == FatResult::OK) {
        dj.obj.fs = fs;
        res = follow_path(&dj, path);
        if (res == FatResult::OK) {
            if (dj.fn[NSFLAG] & (NS_DOT | NS_NONAME)) {
                res = FatResult::INVALID_NAME;
            } else if (dj.obj.attr & AM_RDO) {
                res = FatResult::DENIED;
            }
        }
        if (res == FatResult::OK) {
            dclst = ld_clust(fs, dj.dir);
            if (dj.obj.attr & AM_DIR) {
                sdj.obj.fs = fs;
                sdj.obj.sclust = dclst;
                res = dir_sdi(&sdj, 0);
                if (res == FatResult::OK) {
                    res = dir_read(&sdj, 0);
                    if (res == FatResult::OK) res = FatResult::DENIED;
                    if (res == FatResult::NO_FILE) res = FatResult::OK;
                }
            }
        }
        if (res == FatResult::OK) {
            res = dir_remove(&dj);
            if (res == FatResult::OK && dclst != 0) {
                res = remove_chain(&dj.obj, dclst, 0);
            }
            if (res == FatResult::OK) res = sync_fs(fs);
        }
    }

    return res;
}

// ============================================================================
// Public API: mkdir
// ============================================================================

FatResult FatFs::mkdir(const char* path) noexcept {
    FatResult res;
    FatFsVolume* fs;
    FatDir dj{};
    FatObjId sobj{};
    uint32_t dcl, pcl, tm;

    res = mount_volume(&path, &fs, FA_WRITE);
    if (res == FatResult::OK) {
        dj.obj.fs = fs;
        res = follow_path(&dj, path);
        if (res == FatResult::OK) {
            res = (dj.fn[NSFLAG] & (NS_DOT | NS_NONAME)) ? FatResult::INVALID_NAME : FatResult::EXIST;
        }
        if (res == FatResult::NO_FILE) {
            sobj.fs = fs;
            dcl = create_chain(&sobj, 0);
            res = FatResult::OK;
            if (dcl == 0) res = FatResult::DENIED;
            if (dcl == 1) res = FatResult::INT_ERR;
            if (dcl == 0xFFFFFFFF) res = FatResult::DISK_ERR;
            tm = get_fattime();
            if (res == FatResult::OK) {
                res = dir_clear(fs, dcl);
                if (res == FatResult::OK) {
                    // Create dot entries (FAT only, no exFAT)
                    memset(fs->win + DIR_Name, ' ', 11);
                    fs->win[DIR_Name] = '.';
                    fs->win[DIR_Attr] = AM_DIR;
                    st_32(fs->win + DIR_ModTime, tm);
                    st_clust(fs, fs->win, dcl);
                    memcpy(fs->win + SZDIRE, fs->win, SZDIRE);
                    fs->win[SZDIRE + 1] = '.';
                    pcl = dj.obj.sclust;
                    st_clust(fs, fs->win + SZDIRE, pcl);
                    fs->wflag = 1;
                    res = dir_register(&dj);
                }
            }
            if (res == FatResult::OK) {
                st_32(dj.dir + DIR_CrtTime, tm);
                st_32(dj.dir + DIR_ModTime, tm);
                st_clust(fs, dj.dir, dcl);
                dj.dir[DIR_Attr] = AM_DIR;
                fs->wflag = 1;
                if (res == FatResult::OK) {
                    res = sync_fs(fs);
                }
            } else {
                remove_chain(&sobj, dcl, 0);
            }
        }
    }

    return res;
}

// ============================================================================
// Public API: rename
// ============================================================================

FatResult FatFs::rename(const char* path_old, const char* path_new) noexcept {
    FatResult res;
    FatFsVolume* fs;
    FatDir djo{}, djn{};
    uint8_t buf[SZDIRE];
    uint8_t* dir;

    get_ldnumber(&path_new);
    res = mount_volume(&path_old, &fs, FA_WRITE);
    if (res == FatResult::OK) {
        djo.obj.fs = fs;
        res = follow_path(&djo, path_old);
        if (res == FatResult::OK) {
            if (djo.fn[NSFLAG] & (NS_DOT | NS_NONAME)) {
                res = FatResult::INVALID_NAME;
            }
        }
        if (res == FatResult::OK) {
            // At FAT/FAT32 volume
            memcpy(buf, djo.dir, SZDIRE);
            memcpy(&djn, &djo, sizeof djn);
            res = follow_path(&djn, path_new);
            if (res == FatResult::OK) {
                res = (djn.obj.sclust == djo.obj.sclust && djn.dptr == djo.dptr) ? FatResult::NO_FILE
                                                                                  : FatResult::EXIST;
            }
            if (res == FatResult::NO_FILE) {
                res = dir_register(&djn);
                if (res == FatResult::OK) {
                    dir = djn.dir;
                    memcpy(dir + 13, buf + 13, SZDIRE - 13);
                    dir[DIR_Attr] = buf[DIR_Attr];
                    if (!(dir[DIR_Attr] & AM_DIR)) dir[DIR_Attr] |= AM_ARC;
                    fs->wflag = 1;
                    if ((dir[DIR_Attr] & AM_DIR) && djo.obj.sclust != djn.obj.sclust) {
                        LBA_t sect2 = clst2sect(fs, ld_clust(fs, dir));
                        if (sect2 == 0) {
                            res = FatResult::INT_ERR;
                        } else {
                            res = move_window(fs, sect2);
                            dir = fs->win + SZDIRE * 1;
                            if (res == FatResult::OK && dir[1] == '.') {
                                st_clust(fs, dir, djn.obj.sclust);
                                fs->wflag = 1;
                            }
                        }
                    }
                }
            }
            if (res == FatResult::OK) {
                res = dir_remove(&djo);
                if (res == FatResult::OK) {
                    res = sync_fs(fs);
                }
            }
        }
    }

    return res;
}

} // namespace umi::fs::fat
