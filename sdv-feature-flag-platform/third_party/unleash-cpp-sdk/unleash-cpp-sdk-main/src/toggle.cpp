#include "unleash/Domain/toggle.hpp"

namespace unleash {

Toggle::Toggle(std::string p_name, bool p_enabled, bool p_impressionData, Variant p_variant)
    : _name(std::move(p_name)), _enabled(p_enabled), _impressionData(p_impressionData), _variant(std::move(p_variant))

{}

const std::string& Toggle::name() const {
    return _name;
}

bool Toggle::enabled() const {
    return _enabled;
}

const Variant& Toggle::variant() const {
    return _variant;
}

bool Toggle::impressionData() const {
    return _impressionData;
}

} // namespace unleash