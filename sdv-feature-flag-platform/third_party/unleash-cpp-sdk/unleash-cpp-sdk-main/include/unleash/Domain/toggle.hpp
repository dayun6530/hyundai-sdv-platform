#pragma once
#include "unleash/Domain/variant.hpp"
#include <string>
#include <utility>

namespace unleash {

class Toggle final {

  public:
    explicit Toggle(std::string p_name, bool p_enabled = false, bool p_impressionData = false,
                    Variant p_variant = Variant::disabledFactory());
    // getters:
    const std::string& name() const;
    bool enabled() const;
    const Variant& variant() const;
    bool impressionData() const;

  private:
    std::string _name;
    bool _enabled = false;
    Variant _variant = Variant::disabledFactory();
    bool _impressionData = false;
};

} // namespace unleash