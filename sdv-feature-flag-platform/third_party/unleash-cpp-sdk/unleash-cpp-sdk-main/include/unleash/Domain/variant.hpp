#pragma once

#include <optional>
#include <string>
#include <utility>

namespace unleash {
class Variant final {

  public:
    class Payload final {
      public:
        Payload() = default;

        Payload(std::string name, std::string value);

        bool empty() const;

        const std::string& type() const;

        const std::string& value() const;

        friend bool operator==(const Payload& a, const Payload& b) {
            return a._type == b._type && a._value == b._value;
        }
        friend bool operator!=(const Payload& a, const Payload& b) {
            return !(a == b);
        }

      private:
        std::string _type;
        std::string _value;
    };

    Variant() = default;

    Variant(std::string name, bool enabled = false, std::optional<Payload> payload = std::nullopt);

    const std::string& name() const;

    bool enabled() const;

    bool hasPayload() const;

    const std::optional<Payload>& payload() const;

    static Variant disabledFactory();

    friend bool operator==(const Variant& a, const Variant& b) {
        return a._name == b._name && a._enabled == b._enabled && a._payload == b._payload;
    }
    friend bool operator!=(const Variant& a, const Variant& b) {
        return !(a == b);
    }

  private:
    std::string _name;
    bool _enabled = false;
    std::optional<Payload> _payload;
};

} // namespace unleash