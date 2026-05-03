#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sdv::platform::deployment {

class VehicleRegistry {
public:
    struct Vehicle {
        std::string car_id;
        std::string model;
        std::string region;
        std::string group;
        std::string status;
    };

    /// Loads `vehicles.json` and constructs a registry.
    /// On failure, returns std::nullopt and fills \p error with a descriptive message including \p path.
    static std::optional<VehicleRegistry> loadFromFile(const std::string& path, std::string& error);

    const std::vector<Vehicle>& allVehicles() const { return vehicles_; }

    std::optional<Vehicle> getByCarId(const std::string& car_id) const;

    std::vector<Vehicle> findByGroup(const std::string& group) const;
    std::vector<Vehicle> findByRegion(const std::string& region) const;
    std::vector<Vehicle> findByModel(const std::string& model) const;
    std::vector<Vehicle> findByGroupAndRegion(const std::string& group, const std::string& region) const;

    /// Lightweight self-check helper (no I/O); returns empty string when OK.
    std::string selfTest() const;

private:
    std::vector<Vehicle> vehicles_;
    std::unordered_map<std::string, std::size_t> byCarId_;

    void index();
    static bool validateVehicle(const Vehicle& v, std::string& error);
};

}  // namespace sdv::platform::deployment
