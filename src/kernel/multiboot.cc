// Copyright 2014, runtime.js project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "multiboot.h"
#include <string>
#include <kernel/boot-services.h>

namespace rt {

Multiboot::Multiboot(void* base) :
    _base(base) {}

MultibootMemoryMapEnumerator::MultibootMemoryMapEnumerator(const Multiboot* multiboot)
    :	mmap_start_(0),
        mmap_current_(0),
        mmap_len_(0) {
    uintptr_t base_addr = reinterpret_cast<uintptr_t>(multiboot->base_address());
    uint32_t mmap_len = 0;
    uint32_t mmap_addr = 0;

    memcpy(&mmap_len, reinterpret_cast<void*>(base_addr +
        offsetof(MultibootStruct, mmap_len)), sizeof(uint32_t));

    memcpy(&mmap_addr, reinterpret_cast<void*>(base_addr +
        offsetof(MultibootStruct, mmap_addr)), sizeof(uint32_t));

    printf("Memory map addr %d, len %d\n", mmap_addr, mmap_len);

    if (0 == mmap_addr || 0 == mmap_len) {
        GLOBAL_boot_services()->FatalError("Invalid memory map provided.");
    }

    RT_ASSERT(mmap_addr);
    RT_ASSERT(mmap_len);
    mmap_start_ = mmap_addr;
    mmap_current_ = mmap_addr;
    mmap_len_ = mmap_len;
}

common::MemoryZone MultibootMemoryMapEnumerator::NextAvailableMemory() {
    if (mmap_current_ >= mmap_start_ + mmap_len_) {
        return common::MemoryZone(nullptr, 0);
    }

    MultibootMemoryMapEntry* entry =
        reinterpret_cast<MultibootMemoryMapEntry*>(mmap_current_);
    RT_ASSERT(entry);
    uint32_t size = 0;
    uint64_t base_addr = 0;
    uint64_t length = 0;
    uint32_t type = 0;
    memcpy(&size, reinterpret_cast<void*>(mmap_current_ +
        offsetof(MultibootMemoryMapEntry, size)), sizeof(uint32_t));

    RT_ASSERT(size);
    memcpy(&base_addr, reinterpret_cast<void*>(mmap_current_ +
        offsetof(MultibootMemoryMapEntry, base_addr)), sizeof(uint64_t));

    memcpy(&length, reinterpret_cast<void*>(mmap_current_ +
        offsetof(MultibootMemoryMapEntry, length)), sizeof(uint64_t));

    memcpy(&type, reinterpret_cast<void*>(mmap_current_ +
        offsetof(MultibootMemoryMapEntry, type)), sizeof(uint32_t));

    mmap_current_ += size + sizeof(uint32_t);
    if (type != 1) {
        return NextAvailableMemory();
    }

    return common::MemoryZone(reinterpret_cast<void*>(base_addr), length);
}

} // namespace rt
