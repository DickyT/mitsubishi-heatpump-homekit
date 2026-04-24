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

#include "platform_lock.h"

namespace platform_lock {

RecursiveMutex::~RecursiveMutex() {
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
    }
}

bool RecursiveMutex::init() {
    if (handle_ != nullptr) {
        return true;
    }

    handle_ = xSemaphoreCreateRecursiveMutex();
    return handle_ != nullptr;
}

bool RecursiveMutex::lock(TickType_t timeout) {
    if (!init()) {
        return false;
    }

    return xSemaphoreTakeRecursive(handle_, timeout) == pdTRUE;
}

void RecursiveMutex::unlock() {
    if (handle_ != nullptr) {
        xSemaphoreGiveRecursive(handle_);
    }
}

bool RecursiveMutex::valid() const {
    return handle_ != nullptr;
}

ScopedLock::ScopedLock(RecursiveMutex& mutex, TickType_t timeout) : mutex_(mutex) {
    locked_ = mutex_.lock(timeout);
}

ScopedLock::~ScopedLock() {
    if (locked_) {
        mutex_.unlock();
    }
}

bool ScopedLock::locked() const {
    return locked_;
}

}  // namespace platform_lock
