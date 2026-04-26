#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace unleash::internal {

template <typename E> class Unexpected final {
  public:
    explicit Unexpected(E error) : _error(std::move(error)) {}

    E& error() noexcept {
        return _error;
    }

    const E& error() const noexcept {
        return _error;
    }

  private:
    E _error;
};

template <typename E> Unexpected<std::decay_t<E>> unexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <typename T, typename E> class Expected final {
  public:
    Expected(const T& value) : _hasValue(true), _storage(value) {}

    Expected(T&& value) : _hasValue(true), _storage(std::move(value)) {}

    Expected(const Unexpected<E>& unexp) : _hasValue(false), _storage(unexp.error()) {}

    Expected(Unexpected<E>&& unexp) : _hasValue(false), _storage(std::move(unexp.error())) {}

    bool has_value() const noexcept {
        return _hasValue;
    }

    explicit operator bool() const noexcept {
        return _hasValue;
    }

    T& value() {
        if (!_hasValue) {
            throw std::logic_error("Expected does not contain a value");
        }
        return std::get<T>(_storage);
    }

    const T& value() const {
        if (!_hasValue) {
            throw std::logic_error("Expected does not contain a value");
        }
        return std::get<T>(_storage);
    }

    E& error() {
        if (_hasValue) {
            throw std::logic_error("Expected does not contain an error");
        }
        return std::get<E>(_storage);
    }

    const E& error() const {
        if (_hasValue) {
            throw std::logic_error("Expected does not contain an error");
        }
        return std::get<E>(_storage);
    }

    T* operator->() {
        return &value();
    }

    const T* operator->() const {
        return &value();
    }

    T& operator*() {
        return value();
    }

    const T& operator*() const {
        return value();
    }

  private:
    bool _hasValue;
    std::variant<T, E> _storage;
};

} // namespace unleash::internal
