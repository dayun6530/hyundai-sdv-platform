#include "unleash/Domain/toggleSet.hpp"

namespace unleash {

ToggleSet::ToggleSet(Map p_togglesByName) : _toggles(std::move(p_togglesByName)) {}

ToggleSet::ToggleSet(const std::vector<Toggle>& p_toggles) {
    _toggles.reserve(p_toggles.size());
    for (const auto& t : p_toggles) {
        _toggles.insert({t.name(), t});
    }
}

ToggleSet::ToggleSet(std::vector<Toggle>&& p_toggles) {
    _toggles.reserve(p_toggles.size());
    for (auto& t : p_toggles) {
        const std::string key = t.name();
        _toggles.insert({t.name(), std::move(t)});
    }
}

std::size_t ToggleSet::size() const {
    return _toggles.size();
}

const ToggleSet::Map& ToggleSet::toggles() const {
    return _toggles;
}

bool ToggleSet::contains(const std::string& p_name) const {
    return _toggles.find(p_name) != _toggles.end();
}

const Toggle* ToggleSet::find(const std::string& p_name) const {
    if (_toggles.empty())
        return nullptr;
    const auto it = _toggles.find(p_name);
    if (it == _toggles.end())
        return nullptr;
    return &it->second;
}

bool ToggleSet::isEnabled(const std::string& p_name) const {
    auto toggle = this->find(p_name);
    if (!toggle)
        return false;
    return toggle->enabled();
}

Variant ToggleSet::getVariant(const std::string& p_name) const {
    auto toggle = this->find(p_name);
    if (!toggle)
        return Variant::disabledFactory();
    return toggle->variant();
}

bool ToggleSet::impressionData(const std::string& p_name) const {
    auto toggle = this->find(p_name);
    if (!toggle)
        return false;
    return toggle->impressionData();
}

} // namespace unleash
