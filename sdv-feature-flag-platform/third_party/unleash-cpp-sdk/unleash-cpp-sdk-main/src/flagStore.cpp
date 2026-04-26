#include "unleash/Store/flagStore.hpp"
#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Domain/toggle.hpp"

namespace unleash {

FlagStore::FlagStore() : _snapshot(std::make_shared<const ToggleSet>()), _ready(false) {}

std::shared_ptr<const ToggleSet> FlagStore::snapshot() const noexcept {
    return std::atomic_load(&_snapshot);
}

void FlagStore::replace(std::shared_ptr<const ToggleSet> newSnapshot) noexcept {
    if (!newSnapshot)
        return;
    std::atomic_store(&_snapshot, std::move(newSnapshot));
    _ready.store(true, std::memory_order_release);
}

bool FlagStore::isReady() const noexcept {
    return _ready.load(std::memory_order_acquire);
}

} // namespace unleash
