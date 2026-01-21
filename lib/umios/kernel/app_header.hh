// SPDX-License-Identifier: MIT
// UMI-OS Application Binary Header
// Defines the .umiapp binary format for embedded applications

#pragma once

#include <cstdint>
#include <cstddef>

namespace umi::kernel {

// ============================================================================
// Application Binary Header (.umiapp format)
// ============================================================================

/// Magic number: "UMIA" (UMI Application)
inline constexpr uint32_t APP_MAGIC = 0x414D4955;  // 'U' 'M' 'I' 'A' in little-endian

/// Current ABI version
inline constexpr uint32_t APP_ABI_VERSION = 1;

/// Application target type (determines compatibility)
enum class AppTarget : uint32_t {
    User        = 0,  ///< User app (unsigned, runs on both dev/release kernel)
    Development = 1,  ///< Development app (dev kernel only)
    Release     = 2,  ///< Release app (release kernel only, signature required)
};

/// Application header (placed at the beginning of .umiapp binary)
/// Total size: 128 bytes (aligned for easy parsing)
struct alignas(4) AppHeader {
    // --- Identification (16 bytes) ---
    uint32_t magic;             ///< Must be APP_MAGIC (0x414D4955)
    uint32_t abi_version;       ///< ABI version for compatibility check
    AppTarget target;           ///< Application target type
    uint32_t flags;             ///< Reserved flags (must be 0)

    // --- Entry Points (8 bytes) ---
    uint32_t entry_offset;      ///< Offset to _start() from header
    uint32_t process_offset;    ///< Offset to registered process() (filled by loader)

    // --- Section Sizes (16 bytes) ---
    uint32_t text_size;         ///< .text section size (code)
    uint32_t rodata_size;       ///< .rodata section size (constants)
    uint32_t data_size;         ///< .data section size (initialized data)
    uint32_t bss_size;          ///< .bss section size (uninitialized data)

    // --- Memory Requirements (8 bytes) ---
    uint32_t stack_size;        ///< Required stack size for Control Task
    uint32_t heap_size;         ///< Required heap size (0 if no heap)

    // --- Integrity (8 bytes) ---
    uint32_t crc32;             ///< CRC32 of sections (text + rodata + data)
    uint32_t total_size;        ///< Total image size including header

    // --- Signature (64 bytes) ---
    uint8_t signature[64];      ///< Ed25519 signature (Release apps only)

    // --- Reserved (8 bytes) ---
    uint8_t reserved[8];        ///< Reserved for future use (must be 0)

    // --- Validation Methods ---

    /// Check if magic number is valid
    [[nodiscard]] constexpr bool valid_magic() const noexcept {
        return magic == APP_MAGIC;
    }

    /// Check if ABI version is compatible
    [[nodiscard]] constexpr bool compatible_abi() const noexcept {
        return abi_version == APP_ABI_VERSION;
    }

    /// Get total section size (excluding header)
    [[nodiscard]] constexpr uint32_t sections_size() const noexcept {
        return text_size + rodata_size + data_size;
    }

    /// Get required RAM size for app
    [[nodiscard]] constexpr uint32_t required_ram() const noexcept {
        return data_size + bss_size + stack_size + heap_size;
    }

    /// Get entry point address given load base
    [[nodiscard]] const void* entry_point(const void* base) const noexcept {
        return static_cast<const uint8_t*>(base) + entry_offset;
    }
};

static_assert(sizeof(AppHeader) == 128, "AppHeader must be 128 bytes");
static_assert(alignof(AppHeader) == 4, "AppHeader must be 4-byte aligned");

// ============================================================================
// Application Load Result
// ============================================================================

/// Result of application loading
enum class LoadResult : uint8_t {
    Ok = 0,                 ///< Successfully loaded
    InvalidMagic,           ///< Magic number mismatch
    InvalidVersion,         ///< ABI version incompatible
    InvalidSize,            ///< Size fields inconsistent
    CrcMismatch,            ///< CRC32 verification failed
    SignatureInvalid,       ///< Ed25519 signature invalid (Release apps)
    SignatureRequired,      ///< Signature required but not present
    TargetMismatch,         ///< App target incompatible with kernel build
    OutOfMemory,            ///< Insufficient memory for app
    AlreadyLoaded,          ///< An application is already loaded
};

/// Convert LoadResult to string for debugging
constexpr const char* load_result_str(LoadResult r) noexcept {
    switch (r) {
    case LoadResult::Ok:                return "Ok";
    case LoadResult::InvalidMagic:      return "InvalidMagic";
    case LoadResult::InvalidVersion:    return "InvalidVersion";
    case LoadResult::InvalidSize:       return "InvalidSize";
    case LoadResult::CrcMismatch:       return "CrcMismatch";
    case LoadResult::SignatureInvalid:  return "SignatureInvalid";
    case LoadResult::SignatureRequired: return "SignatureRequired";
    case LoadResult::TargetMismatch:    return "TargetMismatch";
    case LoadResult::OutOfMemory:       return "OutOfMemory";
    case LoadResult::AlreadyLoaded:     return "AlreadyLoaded";
    default:                            return "Unknown";
    }
}

// ============================================================================
// Build Configuration
// ============================================================================

/// Kernel build type (set at compile time)
enum class BuildType : uint8_t {
    Development = 0,  ///< Development build (allows unsigned apps)
    Release     = 1,  ///< Release build (requires signatures for Release apps)
};

#ifndef UMIOS_BUILD_TYPE
#define UMIOS_BUILD_TYPE BuildType::Development
#endif

inline constexpr BuildType KERNEL_BUILD_TYPE = UMIOS_BUILD_TYPE;

} // namespace umi::kernel
