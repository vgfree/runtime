// Copyright 2014, runtime.js project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include <kernel/kernel.h>
#include <kernel/isolate.h>
#include <kernel/cpu.h>
#include <kernel/allocator.h>
#include <kernel/resource.h>
#include <kernel/process.h>
#include <kernel/engine.h>
#include <kernel/system-context.h>
#include <EASTL/vector.h>

namespace rt {

class MallocArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
public:
    virtual void* Allocate(size_t length) { return malloc(length); }
    virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
    virtual void Free(void* data, size_t length) { free(data); }
};

class AcpiManager;

class Engines {
public:
    Engines(uint32_t cpu_count)
        :	cpu_count_(cpu_count),
            _non_isolate_ticks(0),
            proc_mgr_(this) {
        RT_ASSERT(nullptr == GLOBAL_engines());
        RT_ASSERT(this);
        RT_ASSERT(cpu_count >= 1);

        RT_ASSERT(cpu_count >= 2); // System requirement: Dual core machine
                                   // Temporary to support service core

        engines_.reserve(cpu_count);
        engines_execution_.reserve(cpu_count);

        for (uint32_t i = 0; i < cpu_count; ++i) {
            Engine* engine = nullptr;
            if (0 == i) {
                engine = new Engine(EngineType::SERVICE);
            } else {
                engine = new Engine(EngineType::EXECUTION);
                engines_execution_.push_back(engine);
                engine->threads().Create(); // create idle thread
            }
            RT_ASSERT(engine);
            engines_.push_back(engine);
        }

        RT_ASSERT(engines_.size() > 0);
        RT_ASSERT(engines_execution_.size() > 0);

        v8::V8::InitializeICU();
        v8::V8::SetArrayBufferAllocator(new MallocArrayBufferAllocator());

        const char flags[] = "--harmony_promises --harmony_collections";
        v8::V8::SetFlagsFromString(flags, sizeof(flags));
    }

    void Startup() {
        ResourceHandle<Process> p = process_manager().CreateProcess();

        RT_ASSERT(engines_execution_.size() > 0);
        Engine* first_engine = engines_execution_[0];
        RT_ASSERT(first_engine);
        ResourceHandle<EngineThread> st = first_engine->threads().Create();
        p.get()->SetThread(st, 0);

        rt::InitrdFile startup_file = GLOBAL_initrd()->Get("/system/startup.js");
        if (startup_file.IsEmpty()) {
            printf("Unable to load /system/startup.js from initrd.\n");
            abort();
        }

        TransportData data;
        data.SetString(startup_file.Data(), startup_file.Size());

        std::unique_ptr<ThreadMessage> msg(new ThreadMessage(ThreadMessage::Type::EVALUATE,
            ResourceHandle<EngineThread>(), std::move(data)));
        st.get()->PushMessage(std::move(msg));
    }

    uint32_t engines_count() const {
        return engines_.size();
    }

    uint32_t execution_engines_count() const {
        return engines_execution_.size();
    }

    Engine* execution_engine(uint32_t index) const {
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        RT_ASSERT(index < engines_execution_.size());
        RT_ASSERT(engines_execution_[index]);
        return engines_execution_[index];
    }

    bool is_execution_engine(uint32_t engineid) const {
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        RT_ASSERT(engineid < engines_.size());
        RT_ASSERT(engines_[engineid]);
        return EngineType::EXECUTION == engines_[engineid]->type();
    }

    void CpuEnter() {
        uint32_t cpu_id = Cpu::id();
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        RT_ASSERT(cpu_id < cpu_count_);
        RT_ASSERT(cpu_id < engines_.size());
        engines_[cpu_id]->Enter();
    }

    Engine* cpu_engine() const {
        uint32_t cpuid = cpu_id();
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        RT_ASSERT(cpuid < cpu_count_);
        RT_ASSERT(cpuid < engines_.size());
        RT_ASSERT(engines_[cpuid]);
        return engines_[cpuid];
    }

    static uint32_t cpu_id() {
        return Cpu::id();
    }

    uint32_t MsPerTick() const {
        return 10;
    }

    void TimerTick(SystemContextIRQ& irq_context) {
        const Engine* cpuengine = cpu_engine();
        if (cpuengine->is_init()) {
            cpuengine->TimerTick(irq_context);
        } else {
            NonIsolateTick();
        }
    }

    // Special kind of tick generated when no isolates
    // are available in a system. Used for initializion purposes.
    void NonIsolateTick() {
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        ++_non_isolate_ticks;
    }

    void NonIsolateSleep(uint32_t ms) const {
        RT_ASSERT(GLOBAL_engines());
        RT_ASSERT(this == GLOBAL_engines());
        if (0 == ms) return;
        RT_ASSERT(MsPerTick() > 0);

        uint32_t cpuid = cpu_id();
        RT_ASSERT(cpuid < engines_.size());
        RT_ASSERT(!engines_[cpuid]->is_init());

        uint32_t sleep_ticks = ms / MsPerTick();
        if (sleep_ticks == 0) {
            sleep_ticks = 1;
        }

        uint64_t required_ticks = 0;
        {	required_ticks = _non_isolate_ticks + sleep_ticks;
        }

        while (true) {
            uint64_t non_isolate_ticks = 0;
            {	non_isolate_ticks = _non_isolate_ticks;
            }

            if (non_isolate_ticks > required_ticks) {
                break;
            }
            Cpu::WaitPause();
        }
    }

    AcpiManager* acpi_manager();
    ProcessManager& process_manager() { return proc_mgr_; }

    ~Engines() = delete;
    DELETE_COPY_AND_ASSIGN(Engines);
private:
    uint32_t cpu_count_;
    SharedVector<Engine*> engines_;
    SharedVector<Engine*> engines_execution_;
    AcpiManager* _acpi_manager;
    volatile uint64_t _non_isolate_ticks;
    ProcessManager proc_mgr_;

    mutable Locker _platform_locker;
};

} // namespace rt
