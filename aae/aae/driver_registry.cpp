#include "driver_registry.h"
#include "aae_mame_driver.h"
#include <mutex>
#include <cstring>

namespace {
    std::vector<const AAEDriver*>& Registry() {
        static std::vector<const AAEDriver*> g;
        return g;
    }
    std::mutex& RegMutex() {
        static std::mutex m;
        return m;
    }
}

namespace aae {

    void RegisterDriver(const AAEDriver* drv) noexcept {
        if (!drv) return;
        std::lock_guard<std::mutex> lk(RegMutex());
        Registry().push_back(drv);
    }

    const std::vector<const AAEDriver*>& AllDrivers() noexcept {
        return Registry();
    }

    const AAEDriver* FindDriverByName(const char* name) noexcept {
        if (!name) return nullptr;
        for (auto* d : Registry()) {
            if (d && d->name && std::strcmp(d->name, name) == 0)
                return d;
        }
        return nullptr;
    }

    DriverAutoReg::DriverAutoReg(const AAEDriver* d) noexcept {
        RegisterDriver(d);
    }

} // namespace aae
