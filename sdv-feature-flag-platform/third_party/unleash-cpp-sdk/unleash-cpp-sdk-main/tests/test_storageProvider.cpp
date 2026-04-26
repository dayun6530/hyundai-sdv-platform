#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "unleash/Domain/toggle.hpp"
#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Domain/variant.hpp"
#include "unleash/Store/storageProvider.hpp"

namespace {

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        const auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("unleash-cpp-sdk-storage-test-" + std::to_string(ts));
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST(FileStorageProvider, GetReturnsNulloptWhenBackupFileMissing) {
    TempDir dir;
    unleash::FileStorageProvider provider("cppApp", dir.path.string());

    const auto toggles = provider.get();
    EXPECT_FALSE(toggles.has_value());
}

TEST(FileStorageProvider, SaveThenGetRoundTripsToggleSet) {
    TempDir dir;
    unleash::FileStorageProvider provider("cppApp", dir.path.string());

    unleash::ToggleSet::Map m;
    m.emplace("flag-on",
              unleash::Toggle{"flag-on", true, true,
                              unleash::Variant{"variant-a", true, unleash::Variant::Payload{"string", "hello"}}});
    m.emplace("flag-off", unleash::Toggle{"flag-off", false, false});

    const unleash::ToggleSet original(std::move(m));
    provider.save(original);

    const auto loaded = provider.get();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->size(), 2u);

    EXPECT_TRUE(loaded->contains("flag-on"));
    EXPECT_TRUE(loaded->isEnabled("flag-on"));
    EXPECT_TRUE(loaded->impressionData("flag-on"));

    const auto loadedVariant = loaded->getVariant("flag-on");
    EXPECT_EQ(loadedVariant.name(), "variant-a");
    EXPECT_TRUE(loadedVariant.enabled());
    ASSERT_TRUE(loadedVariant.payload().has_value());
    EXPECT_EQ(loadedVariant.payload()->type(), "string");
    EXPECT_EQ(loadedVariant.payload()->value(), "hello");

    EXPECT_TRUE(loaded->contains("flag-off"));
    EXPECT_FALSE(loaded->isEnabled("flag-off"));
    EXPECT_EQ(loaded->getVariant("flag-off"), unleash::Variant::disabledFactory());
}

TEST(FileStorageProvider, GetReturnsNulloptWhenBackupFileIsEmpty) {
    TempDir dir;
    unleash::FileStorageProvider provider("cppApp", dir.path.string());

    const auto backupFile = dir.path / "unleash-backup-cppApp.json";

    { std::ofstream out(backupFile, std::ios::binary); }

    const auto toggles = provider.get();
    EXPECT_FALSE(toggles.has_value());
}

TEST(FileStorageProvider, GetReturnsNulloptWhenBackupFileIsWhitespaceOnly) {
    TempDir dir;
    unleash::FileStorageProvider provider("cppApp", dir.path.string());

    const auto backupFile = dir.path / "unleash-backup-cppApp.json";

    {
        std::ofstream out(backupFile, std::ios::binary);
        out << "   \n\t   ";
    }

    const auto toggles = provider.get();
    EXPECT_FALSE(toggles.has_value());
}

TEST(FileStorageProvider, SaveCreatesMissingDirectories) {
    TempDir dir;
    const auto nestedPath = dir.path / "a" / "b" / "c";

    unleash::FileStorageProvider provider("cppApp", nestedPath.string());

    unleash::ToggleSet::Map m;
    m.emplace("flag-on", unleash::Toggle{"flag-on", true, false});

    const unleash::ToggleSet original(std::move(m));
    provider.save(original);

    const auto loaded = provider.get();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->contains("flag-on"));
    EXPECT_TRUE(loaded->isEnabled("flag-on"));
}

TEST(FileStorageProvider, SaveAndGetWorkWithSanitizedAppName) {
    TempDir dir;
    const std::string appName = "cpp/app:prod?weird name";
    unleash::FileStorageProvider provider(appName, dir.path.string());

    unleash::ToggleSet::Map m;
    m.emplace("flag-on", unleash::Toggle{"flag-on", true, false});

    const unleash::ToggleSet original(std::move(m));
    provider.save(original);

    const auto expectedFile = dir.path / "unleash-backup-cpp_app_prod_weird_name.json";
    EXPECT_TRUE(std::filesystem::exists(expectedFile));

    const auto loaded = provider.get();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->contains("flag-on"));
    EXPECT_TRUE(loaded->isEnabled("flag-on"));
}

TEST(FileStorageProvider, GetReturnsNulloptWhenBackupFileContainsInvalidJson) {
    TempDir dir;
    unleash::FileStorageProvider provider("cppApp", dir.path.string());

    const auto backupFile = dir.path / "unleash-backup-cppApp.json";

    {
        std::ofstream out(backupFile, std::ios::binary);
        out << "{ definitely-not-valid-json ";
    }

    EXPECT_FALSE(provider.get().has_value());
}