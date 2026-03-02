
// -----------------------------------------------------------------------------
// This file is part of the AAE (Another Arcade Emulator) project.
//
// Copyright (C) 2025-2026 Tim Cottrill
//
// This code is released under the GNU General Public License v3.0
// or (at your option) any later version. See the accompanying
// source files and license text for full details.
// -----------------------------------------------------------------------------

#pragma once
#include <vector>

struct AAEDriver; // forward declared (defined in aae_mame_driver.h)

namespace aae {

    // Register a driver (called from each driver TU).
    void RegisterDriver(const AAEDriver* drv) noexcept;

    // All registered drivers (for menus, enumeration).
    const std::vector<const AAEDriver*>& AllDrivers() noexcept;

    // Lookup by short name ("llander", etc.). Returns nullptr if not found.
    const AAEDriver* FindDriverByName(const char* name) noexcept;

    // Helper for static auto-registration.
    struct DriverAutoReg {
        explicit DriverAutoReg(const AAEDriver* d) noexcept;
    };

    // Macro to self-register a driver symbol from its TU.
#define AAE_REGISTER_DRIVER(drv_sym) \
    static ::aae::DriverAutoReg s_autoReg_##drv_sym{ &(drv_sym) };

} // namespace aae
