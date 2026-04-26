#pragma once
#include "unleash/Domain/toggleSet.hpp"

namespace unleash {

class IStorageProvider {
  public:
    virtual ~IStorageProvider() = default;
    virtual const std::optional<ToggleSet> get() = 0;
    virtual void save(const ToggleSet& t) = 0;
};

class LocalStorageProvider final : public IStorageProvider {
  public:
    const std::optional<ToggleSet> get() override {
        return empty_;
    }

    void save(const ToggleSet&) override {
        // Nothing will happen!
    }

  private:
    std::optional<ToggleSet> empty_{};
};

class FileStorageProvider final : public IStorageProvider {
  public:
    explicit FileStorageProvider(std::string appName);

    FileStorageProvider(std::string appName, std::string backupPath);

    const std::optional<ToggleSet> get() override;

    void save(const ToggleSet& t) override;

  private:
    static std::string safeName(std::string s);

    static std::string defaultBackupPath();

    std::string _filePath;
};

} // namespace unleash
