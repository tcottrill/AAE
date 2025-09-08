#pragma once
#include <cstddef>
#include "driver_registry.h"
#include "aae_mame_driver.h"

// Emulates 'driver[i]' returning a const AAEDriver&
struct DriverCompatArray {
    const AAEDriver& operator[](std::size_t i) const {
        const auto& v = aae::AllDrivers();
        // If you prefer no bounds checking, use v[i].
        return *v.at(i);
    }
    // Optional convenience if some code uses a count
    std::size_t size() const {
        return aae::AllDrivers().size();
    }
};

// Global proxy named exactly like the legacy array.
extern const DriverCompatArray driver;
