#include "internal/jsonCodec.hpp"

#include "internal/expected.hpp"

namespace unleash {

namespace {

internal::Expected<Toggle, std::string> decodeToggle(const JsonCodec::json& item) {
    if (!item.is_object()) {
        return internal::unexpected(std::string("toggle entry is not an object"));
    }
    const JsonCodec::json& obj = item;

    auto itName = obj.find("name");
    if (itName == obj.end() || !itName->is_string()) {
        return internal::unexpected(std::string("toggle.name is missing or not a string"));
    }
    std::string toggleName = itName->get<std::string>();

    auto itEnabled = obj.find("enabled");
    if (itEnabled == obj.end() || !itEnabled->is_boolean()) {
        return internal::unexpected(std::string("toggle.enabled is missing or not a boolean"));
    }
    const bool toggleEnabled = itEnabled->get<bool>();

    bool toggleImpression = false;
    auto itImpr = obj.find("impressionData");
    if (itImpr != obj.end()) {
        if (!itImpr->is_boolean()) {
            return internal::unexpected(std::string("toggle.impressionData is not a boolean"));
        }
        toggleImpression = itImpr->get<bool>();
    }

    if (!toggleEnabled) {
        return Toggle{std::move(toggleName), false, toggleImpression};
    }

    auto itVariant = obj.find("variant");
    if (itVariant == obj.end()) {
        return Toggle{std::move(toggleName), true, toggleImpression};
    }
    if (!itVariant->is_object()) {
        return internal::unexpected(std::string("toggle.variant is not an object"));
    }
    const JsonCodec::json& vObj = *itVariant;

    auto itVName = vObj.find("name");
    auto itVEnabled = vObj.find("enabled");
    if (itVName == vObj.end() || !itVName->is_string() || itVName->get<std::string>().empty() ||
        itVEnabled == vObj.end() || !itVEnabled->is_boolean() || !itVEnabled->get<bool>()) {
        return Toggle{std::move(toggleName), toggleEnabled, toggleImpression};
    }

    auto itPayload = vObj.find("payload");
    if (itPayload == vObj.end()) {
        return Toggle{std::move(toggleName), toggleEnabled, toggleImpression,
                      Variant{itVName->get<std::string>(), itVEnabled->get<bool>()}};
    }
    if (!itPayload->is_object()) {
        return internal::unexpected(std::string("toggle.variant.payload is not an object"));
    }
    const JsonCodec::json& pObj = *itPayload;
    auto itType = pObj.find("type");
    auto itValue = pObj.find("value");
    if (itType == pObj.end() || !itType->is_string() || itType->get<std::string>().empty() || itValue == pObj.end() ||
        !itValue->is_string()) {
        return Toggle{std::move(toggleName), toggleEnabled, toggleImpression,
                      Variant{itVName->get<std::string>(), itVEnabled->get<bool>()}};
    }

    return Toggle{std::move(toggleName), toggleEnabled, toggleImpression,
                  Variant{itVName->get<std::string>(), itVEnabled->get<bool>(),
                          Variant::Payload{itType->get<std::string>(), itValue->get<std::string>()}}};
}

} // namespace

internal::Expected<ToggleSet, std::string> JsonCodec::decodeClientFeaturesResponse(const std::string& jsonText) {
    JsonCodec::json root = JsonCodec::json::parse(jsonText, nullptr, false);
    if (root.is_discarded()) {
        return internal::unexpected(std::string("input is not valid JSON"));
    }

    auto itToggles = root.find("toggles");
    if (itToggles == root.end()) {
        return internal::unexpected(std::string("missing toggles field"));
    }
    if (!itToggles->is_array()) {
        return internal::unexpected(std::string("toggles field is not an array"));
    }

    const JsonCodec::json& toggles = *itToggles;

    std::vector<Toggle> vToggles;
    vToggles.reserve(toggles.size());

    std::size_t index = 0;
    for (const auto& item : toggles) {
        auto decodedToggle = decodeToggle(item);
        if (!decodedToggle.has_value()) {
            return internal::unexpected("invalid toggle at index " + std::to_string(index) + ": " +
                                        decodedToggle.error());
        }
        vToggles.emplace_back(std::move(decodedToggle.value()));
        ++index;
    }

    return ToggleSet(std::move(vToggles));
}

std::string JsonCodec::encodeClientFeaturesResponse(const ToggleSet& toggleSet) {
    json arr = json::array();
    for (const auto& [name, toggle] : toggleSet.toggles()) {
        json t;
        t["name"] = toggle.name();
        t["enabled"] = toggle.enabled();
        t["impressionData"] = toggle.impressionData();

        const Variant& v = toggle.variant();
        json vj;
        vj["name"] = v.name();
        vj["enabled"] = v.enabled();
        const auto& payload = v.payload();
        if (payload.has_value() && !payload->empty()) {
            json pj;
            pj["type"] = payload->type();
            pj["value"] = payload->value();
            vj["payload"] = std::move(pj);
        }
        t["variant"] = std::move(vj);

        arr.push_back(std::move(t));
    }
    json root;
    root["toggles"] = std::move(arr);
    return root.dump();
}

// Domain -> request body
std::string JsonCodec::encodeContextRequestBody(const Context& ctx) {
    json j = nlohmann::json::object();
    j["appName"] = ctx.getAppName();
    j["sessionId"] = ctx.getSessionId();

    if (ctx.getEnvironment())
        j["environment"] = *ctx.getEnvironment();
    if (ctx.getUserId())
        j["userId"] = *ctx.getUserId();
    if (ctx.getRemoteAddress())
        j["remoteAddress"] = *ctx.getRemoteAddress();
    if (ctx.getCurrentTime())
        j["currentTime"] = *ctx.getCurrentTime();
    if (!ctx.getProperties().empty()) {
        json vProperties;
        for (const auto& [key, value] : ctx.getProperties())
            vProperties[key] = value;
        j["properties"] = vProperties;
    }

    json root;
    root["context"] = j;
    return root.dump();
}

std::string JsonCodec::encodeMetricsRequestBody(const MetricList& p_metricList, const std::string& p_start,
                                                const std::string& p_end, const std::string& p_appName,
                                                const std::string& p_instanceId) {
    json jBucket = nlohmann::json::object();
    jBucket["start"] = p_start;
    jBucket["stop"] = p_end;

    json jToggles = nlohmann::json::object();

    // MetricList::getList() -> map<string, MetricToggle> (or unordered_map)
    const auto& list = p_metricList.getList();
    for (const auto& [toggleName, metricToggle] : list) {
        json jToggle = nlohmann::json::object();
        jToggle["yes"] = metricToggle.getYesCount();
        jToggle["no"] = metricToggle.getNoCount();

        json jVariants = nlohmann::json::object();
        for (const auto& [variantName, count] : metricToggle.getVariantStats()) {
            jVariants[variantName] = count;
        }
        jToggle["variants"] = std::move(jVariants);

        jToggles[toggleName] = std::move(jToggle);
    }

    jBucket["toggles"] = std::move(jToggles);

    json jRoot = nlohmann::json::object();
    jRoot["bucket"] = std::move(jBucket);
    jRoot["appName"] = p_appName;
    jRoot["instanceId"] = p_instanceId;

    return jRoot.dump();
}
} // namespace unleash
