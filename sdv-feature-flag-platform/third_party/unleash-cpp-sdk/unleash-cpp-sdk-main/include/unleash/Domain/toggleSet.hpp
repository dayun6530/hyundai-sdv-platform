#pragma once
#include "unleash/Domain/variant.hpp"
#include "unleash/Domain/toggle.hpp"
#include "string"
#include <utility>
#include <unordered_map>
#include <vector>

namespace unleash {
class ToggleSet final {
  public:
    using Map = std::unordered_map<std::string, Toggle>;

    ToggleSet() = default;

    explicit ToggleSet(Map p_togglesByName);
    // This insert from vector of toggles will follow the rule: First one Wins:
    //   If two toggles have the same name, the first will be added to map but the second no!
    explicit ToggleSet(const std::vector<Toggle>& p_toggles);

    explicit ToggleSet(std::vector<Toggle>&& p_toggles);

    std::size_t size() const;

    const Map& toggles() const;

    bool contains(const std::string& p_name) const;

    bool isEnabled(const std::string& p_name) const;

    Variant getVariant(const std::string& p_name) const;

    bool impressionData(const std::string& p_name) const;

  private:
    const Toggle* find(const std::string& p_name) const;
    Map _toggles;
};

} // namespace unleash
