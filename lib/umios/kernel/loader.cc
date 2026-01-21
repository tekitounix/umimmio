// SPDX-License-Identifier: MIT
// UMI-OS Application Loader Implementation

#include "loader.hh"
#include "mpu_config.hh"
#include <cstring>

namespace umi::kernel {

// ============================================================================
// AppLoader Implementation
// ============================================================================

LoadResult AppLoader::load(const uint8_t* image, size_t size) noexcept {
    // Check if already loaded
    if (runtime_.state != AppState::None) {
        return LoadResult::AlreadyLoaded;
    }
    
    // Validate minimum size
    if (size < sizeof(AppHeader)) {
        return LoadResult::InvalidSize;
    }
    
    // Get header
    const auto* header = reinterpret_cast<const AppHeader*>(image);
    
    // Validate header
    auto result = validate_header(header, size);
    if (result != LoadResult::Ok) {
        return result;
    }
    
    // Verify CRC
    if (!verify_crc(header, image)) {
        return LoadResult::CrcMismatch;
    }
    
    // Verify signature for Release apps
    if (header->target == AppTarget::Release) {
        if (!verify_signature(header, image)) {
            return LoadResult::SignatureInvalid;
        }
    }
    
    // Setup memory layout
    if (!setup_memory(header)) {
        return LoadResult::OutOfMemory;
    }
    
    // Copy sections to RAM
    copy_sections(header, image);
    
    // Configure MPU for isolation
    configure_mpu();
    
    // Update state
    loaded_header_ = header;
    runtime_.state = AppState::Loaded;
    
    return LoadResult::Ok;
}

void AppLoader::unload() noexcept {
    if (runtime_.state == AppState::None) {
        return;
    }
    
    // Clear memory
    if (runtime_.base != nullptr && app_ram_size_ > 0) {
        std::memset(runtime_.base, 0, app_ram_size_);
    }
    
    // Reset state
    runtime_.clear();
    loaded_header_ = nullptr;
}

bool AppLoader::start() noexcept {
    if (runtime_.state != AppState::Loaded) {
        return false;
    }
    
    if (runtime_.entry == nullptr) {
        return false;
    }
    
    runtime_.state = AppState::Running;
    
    // Note: Actual task creation and unprivileged execution
    // is handled by the kernel scheduler, not here.
    // This just marks the app as ready to run.
    
    return true;
}

void AppLoader::terminate(int exit_code) noexcept {
    runtime_.exit_code = exit_code;
    runtime_.state = AppState::Terminated;
    
    // Clear processor registration
    runtime_.processor = nullptr;
    runtime_.process_fn = nullptr;
}

void AppLoader::suspend() noexcept {
    if (runtime_.state == AppState::Running) {
        runtime_.state = AppState::Suspended;
    }
}

void AppLoader::resume() noexcept {
    if (runtime_.state == AppState::Suspended) {
        runtime_.state = AppState::Running;
    }
}

int AppLoader::register_processor(void* processor, ProcessFn process_fn) noexcept {
    if (runtime_.state != AppState::Running) {
        return -1;
    }
    
    if (processor == nullptr || process_fn == nullptr) {
        return -1;
    }
    
    runtime_.processor = processor;
    runtime_.process_fn = process_fn;
    
    return 0;
}

// ============================================================================
// Internal Methods
// ============================================================================

LoadResult AppLoader::validate_header(const AppHeader* header, size_t image_size) noexcept {
    // Check magic
    if (!header->valid_magic()) {
        return LoadResult::InvalidMagic;
    }
    
    // Check ABI version
    if (!header->compatible_abi()) {
        return LoadResult::InvalidVersion;
    }
    
    // Check target compatibility
    switch (header->target) {
    case AppTarget::User:
        // User apps run on any kernel
        break;
        
    case AppTarget::Development:
        // Development apps only on development kernel
        if constexpr (KERNEL_BUILD_TYPE != BuildType::Development) {
            return LoadResult::TargetMismatch;
        }
        break;
        
    case AppTarget::Release:
        // Release apps only on release kernel
        if constexpr (KERNEL_BUILD_TYPE != BuildType::Release) {
            return LoadResult::TargetMismatch;
        }
        break;
        
    default:
        return LoadResult::InvalidSize;  // Unknown target
    }
    
    // Check size consistency
    size_t expected_size = sizeof(AppHeader) + header->sections_size();
    if (header->total_size != expected_size || image_size < expected_size) {
        return LoadResult::InvalidSize;
    }
    
    // Check entry point is within text section
    if (header->entry_offset < sizeof(AppHeader) ||
        header->entry_offset >= sizeof(AppHeader) + header->text_size) {
        return LoadResult::InvalidSize;
    }
    
    return LoadResult::Ok;
}

bool AppLoader::verify_crc(const AppHeader* header, const uint8_t* image) noexcept {
    // Calculate CRC of sections (after header)
    const uint8_t* sections_start = image + sizeof(AppHeader);
    size_t sections_len = header->sections_size();
    
    uint32_t calculated = crc32(sections_start, sections_len);
    
    return calculated == header->crc32;
}

bool AppLoader::verify_signature(const AppHeader* header, const uint8_t* image) noexcept {
    // TODO: Implement Ed25519 signature verification
    // For now, reject all Release apps in development
    (void)header;
    (void)image;
    
    if constexpr (KERNEL_BUILD_TYPE == BuildType::Development) {
        // In development mode, skip signature verification
        return true;
    }
    
    // In release mode, verify signature
    // return ed25519_verify(image, header->total_size, header->signature, public_key);
    return false;  // Not implemented yet
}

bool AppLoader::setup_memory(const AppHeader* header) noexcept {
    if (app_ram_base_ == nullptr || app_ram_size_ == 0) {
        return false;
    }
    
    // Calculate required memory
    size_t required = header->required_ram();
    if (required > app_ram_size_) {
        return false;
    }
    
    // Memory layout:
    // [base] -> .data/.bss
    // [base + data + bss] -> heap (if any)
    // [top - stack_size] -> stack (grows down)
    
    uint8_t* base = static_cast<uint8_t*>(app_ram_base_);
    
    runtime_.base = base;
    runtime_.data_start = base;
    
    // Stack at the end of app RAM
    runtime_.stack_base = base + app_ram_size_ - header->stack_size;
    runtime_.stack_top = base + app_ram_size_;
    
    return true;
}

void AppLoader::copy_sections(const AppHeader* header, const uint8_t* image) noexcept {
    const uint8_t* src = image + sizeof(AppHeader);
    
    // .text section - execute from flash (XIP) or copy to RAM
    // For Cortex-M, we can execute from flash directly
    runtime_.text_start = const_cast<uint8_t*>(src);
    
    // Entry point (set bit 0 for Thumb mode on Cortex-M)
    uintptr_t entry_addr = reinterpret_cast<uintptr_t>(src) + (header->entry_offset - sizeof(AppHeader));
    entry_addr |= 1;  // Thumb bit
    runtime_.entry = reinterpret_cast<void(*)()>(entry_addr);
    
    // .data section - copy to RAM
    src += header->text_size + header->rodata_size;
    std::memcpy(runtime_.data_start, src, header->data_size);
    
    // .bss section - zero initialize
    uint8_t* bss_start = static_cast<uint8_t*>(runtime_.data_start) + header->data_size;
    std::memset(bss_start, 0, header->bss_size);
}

void AppLoader::configure_mpu() noexcept {
    // MPU configuration is platform-specific
    // Call the MPU configuration module
    // mpu::configure_app_regions(runtime_, loaded_header_);
}

} // namespace umi::kernel
