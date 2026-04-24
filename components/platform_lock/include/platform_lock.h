/****************************************************************************
 * Kiri Bridge
 * CN105 HomeKit controller for Mitsubishi heat pumps
 * https://kiri.dkt.moe
 * https://github.com/DickyT/kiri-homekit
 *
 * Copyright (c) 2026
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 ****************************************************************************/

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace platform_lock {

class RecursiveMutex {
public:
    RecursiveMutex() = default;
    ~RecursiveMutex();

    RecursiveMutex(const RecursiveMutex&) = delete;
    RecursiveMutex& operator=(const RecursiveMutex&) = delete;

    bool init();
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();
    bool valid() const;

private:
    SemaphoreHandle_t handle_ = nullptr;
};

class ScopedLock {
public:
    explicit ScopedLock(RecursiveMutex& mutex, TickType_t timeout = portMAX_DELAY);
    ~ScopedLock();

    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

    bool locked() const;

private:
    RecursiveMutex& mutex_;
    bool locked_ = false;
};

}  // namespace platform_lock
