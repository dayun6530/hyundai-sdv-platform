#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace sdv::platform::deployment {

class VersionManager {
public:
    struct VersionInfo {
        std::string feature;  // ADAS | Navigation | Energy
        char slot{'A'};       // 'A' | 'B'
        std::string version;  // e.g. v0.1
        std::string role;     // stable | candidate
        std::string binary;   // e.g. AdasFeature_A
    };

    // Internal representation of each slot/feature in the JSON file.
    struct SlotData {
        std::string version;
        std::string role;
        std::string binary;
    };
    struct FeatureData {
        std::optional<SlotData> a;
        std::optional<SlotData> b;
    };

    /// Loads `deploy/version_map.json`.
    /// On failure, returns std::nullopt and fills \p error with a message including \p path.
    static std::optional<VersionManager> loadFromFile(const std::string& path, std::string& error);

    bool hasFeature(const std::string& feature) const;
    bool hasBothSlots(const std::string& feature) const;

    std::optional<VersionInfo> getInfo(const std::string& feature, char slot, std::string& error) const;
    std::optional<std::string> getBinary(const std::string& feature, char slot, std::string& error) const;
    std::optional<std::string> getRole(const std::string& feature, char slot, std::string& error) const;

    /// Minimal invariants for sanity-checking sample data (no I/O); returns empty string when OK.
    std::string selfTest() const;

private:
    std::unordered_map<std::string, FeatureData> features_;

    static bool isValidSlot(char slot);
    static bool isSupportedFeatureName(const std::string& feature);
};

}  // namespace sdv::platform::deployment
