#include "unleash/Store/storageProvider.hpp"

#include "internal/jsonCodec.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace unleash {

namespace {

bool isWhitespaceOnly(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

} // namespace

FileStorageProvider::FileStorageProvider(std::string appName)
    : FileStorageProvider(std::move(appName), defaultBackupPath()) {}

FileStorageProvider::FileStorageProvider(std::string appName, std::string backupPath) {
    const auto fileName = std::string("unleash-backup-") + safeName(std::move(appName)) + ".json";
    _filePath = (std::filesystem::path(std::move(backupPath)) / fileName).string();
}

const std::optional<ToggleSet> FileStorageProvider::get() {
    std::ifstream in(_filePath, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty() || isWhitespaceOnly(content)) {
        return std::nullopt;
    }

    auto decoded = JsonCodec::decodeClientFeaturesResponse(content);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    return std::move(decoded.value());
}

void FileStorageProvider::save(const ToggleSet& t) {
    std::error_code ec;
    const std::filesystem::path path(_filePath);

    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return;
        }
    }

    std::ofstream out(_filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << JsonCodec::encodeClientFeaturesResponse(t);
    out.flush();
}

std::string FileStorageProvider::safeName(std::string s) {
    std::replace_if(
        s.begin(), s.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_'; },
        '_');
    return s;
}

std::string FileStorageProvider::defaultBackupPath() {
    std::error_code ec;
    const auto p = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return ".";
    }
    return p.string();
}

} // namespace unleash
