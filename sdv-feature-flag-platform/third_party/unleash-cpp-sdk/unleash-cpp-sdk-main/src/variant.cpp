#include "unleash/Domain/variant.hpp"

namespace unleash {

Variant::Payload::Payload(std::string type, std::string value) : _type(std::move(type)), _value(std::move(value)) {}

bool Variant::Payload::empty() const {
    return _type.empty();
}

const std::string& Variant::Payload::type() const {
    return _type;
}

const std::string& Variant::Payload::value() const {
    return _value;
}

Variant::Variant(std::string name, bool enabled, std::optional<Payload> payload)
    : _name(std::move(name)), _enabled(enabled) {
    if (_enabled && payload.has_value())
        _payload = payload;
}

const std::string& Variant::name() const {
    return _name;
}

bool Variant::enabled() const {
    return _enabled;
}

bool Variant::hasPayload() const {
    return _payload.has_value();
}

const std::optional<Variant::Payload>& Variant::payload() const {
    return _payload;
}

Variant Variant::disabledFactory() {
    return Variant("disabled");
}

} // namespace unleash