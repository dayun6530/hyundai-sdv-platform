#pragma once
#include <atomic>
#include <memory>
#include <utility>

namespace unleash {

class ToggleSet;
class Toggle;

class FlagStore final {

  public:
    FlagStore();

    std::shared_ptr<const ToggleSet> snapshot() const noexcept;

    void replace(std::shared_ptr<const ToggleSet> newSnapshot) noexcept;

    bool isReady() const noexcept;

  private:
    mutable std::shared_ptr<const ToggleSet> _snapshot;
    std::atomic_bool _ready;
};

} // namespace unleash