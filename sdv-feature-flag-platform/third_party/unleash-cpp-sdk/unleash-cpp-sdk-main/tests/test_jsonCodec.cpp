#include <gtest/gtest.h>

#include "internal/jsonCodec.hpp"
#include "unleash/Domain/variant.hpp"
#include "unleash/Domain/context.hpp"
#include "unleash/Metrics/metricList.hpp"

using nlohmann::json;
using unleash::Context;
using unleash::JsonCodec;
using unleash::MetricList;
using unleash::ToggleSet;
using unleash::Variant;

TEST(JsonCodecDecodeClientFeaturesResponse, ParsesValidTogglesAndVariantsWithPayload) {
    const std::string jsonText = R"json(
    {
      "toggles": [
        {
          "name": "test-flag",
          "enabled": true,
          "variant": {
            "name": "hello",
            "enabled": true,
            "payload": { "type": "number", "value": "1234" },
            "feature_enabled": true,
            "featureEnabled": true
          },
          "impressionData": false
        },
        {
          "name": "test-json",
          "enabled": true,
          "variant": {
            "name": "hello",
            "enabled": true,
            "payload": { "type": "string", "value": "world" },
            "feature_enabled": true,
            "featureEnabled": true
          },
          "impressionData": false
        }
      ]
    }
    )json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);
    ASSERT_TRUE(set.has_value());
    std::cout << "##########################################size is: " << set->size() << std::endl;
    EXPECT_EQ(set->size(), 2u);

    EXPECT_TRUE(set->contains("test-flag"));
    EXPECT_TRUE(set->isEnabled("test-flag"));

    Variant v1 = set->getVariant("test-flag");
    EXPECT_EQ(v1.name(), "hello");
    EXPECT_TRUE(v1.enabled());
    EXPECT_TRUE(v1.hasPayload());
    ASSERT_TRUE(v1.payload().has_value());
    EXPECT_EQ(v1.payload()->type(), "number");
    EXPECT_EQ(v1.payload()->value(), "1234");

    EXPECT_TRUE(set->contains("test-json"));
    EXPECT_TRUE(set->isEnabled("test-json"));

    Variant v2 = set->getVariant("test-json");
    EXPECT_EQ(v2.name(), "hello");
    EXPECT_TRUE(v2.enabled());
    EXPECT_TRUE(v2.hasPayload());
    ASSERT_TRUE(v2.payload().has_value());
    EXPECT_EQ(v2.payload()->type(), "string");
    EXPECT_EQ(v2.payload()->value(), "world");
}

TEST(JsonCodecDecodeClientFeaturesResponse, ToggleDisabledForcesDisabledVariant) {
    const std::string jsonText = R"json(
    {
      "toggles": [
        {
          "name": "flag-off",
          "enabled": false,
          "variant": {
            "name": "hello",
            "enabled": true,
            "payload": { "type": "string", "value": "world" }
          },
          "impressionData": true
        }
      ]
    }
    )json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);
    ASSERT_TRUE(set.has_value());

    EXPECT_EQ(set->size(), 1u);
    EXPECT_TRUE(set->contains("flag-off"));
    EXPECT_FALSE(set->isEnabled("flag-off"));

    Variant v = set->getVariant("flag-off");
    EXPECT_EQ(v, Variant::disabledFactory());
}

TEST(JsonCodecDecodeClientFeaturesResponse, MissingVariantFallsBackToDisabledVariant) {
    const std::string jsonText = R"json(
    {
      "toggles": [
        {
          "name": "no-variant",
          "enabled": true,
          "impressionData": false
        }
      ]
    }
    )json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);
    ASSERT_TRUE(set.has_value());

    EXPECT_EQ(set->size(), 1u);
    EXPECT_TRUE(set->contains("no-variant"));
    EXPECT_TRUE(set->isEnabled("no-variant"));

    Variant v = set->getVariant("no-variant");
    EXPECT_EQ(v, Variant::disabledFactory());
}

TEST(JsonCodecDecodeClientFeaturesResponse, InvalidJsonReturnsNoValue) {
    const std::string invalid = R"({ "toggles": [ )";

    auto set = JsonCodec::decodeClientFeaturesResponse(invalid);

    EXPECT_FALSE(set.has_value());
    EXPECT_EQ(set.error(), "input is not valid JSON");
}

TEST(JsonCodecDecodeClientFeaturesResponse, MissingTogglesFieldReturnsNoValue) {
    const std::string jsonText = R"json({ "foo": 123 })json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);

    EXPECT_FALSE(set.has_value());
    EXPECT_EQ(set.error(), "missing toggles field");
}

TEST(JsonCodecDecodeClientFeaturesResponse, DuplicateToggleNamesFirstOneWins) {
    const std::string jsonText = R"json(
    {
      "toggles": [
        { "name": "dup", "enabled": true,  "variant": { "name": "A", "enabled": true } },
        { "name": "dup", "enabled": false, "variant": { "name": "B", "enabled": true } }
      ]
    }
    )json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);
    ASSERT_TRUE(set.has_value());

    EXPECT_TRUE(set->contains("dup"));
    EXPECT_TRUE(set->isEnabled("dup"));
    EXPECT_EQ(set->getVariant("dup").name(), "A");
}

TEST(JsonCodecDecodeClientFeaturesResponse, AnyInvalidToggleEntryReturnsNoValue) {
    const std::string jsonText = R"json(
    {
      "toggles": [
        { "name": "ok", "enabled": true, "variant": { "name": "A", "enabled": true } },
        { "enabled": true, "variant": { "name": "B", "enabled": true } }
      ]
    }
    )json";

    auto set = JsonCodec::decodeClientFeaturesResponse(jsonText);

    EXPECT_FALSE(set.has_value());
    EXPECT_EQ(set.error(), "invalid toggle at index 1: toggle.name is missing or not a string");
}

TEST(JsonCodecEncodeContextRequestBody, ProducesValidJsonWithContextRoot) {
    Context ctx("unleash-demo2", "24876069");

    const std::string body = JsonCodec::encodeContextRequestBody(ctx);

    nlohmann::json j = nlohmann::json::parse(body);
    ASSERT_TRUE(j.is_object());
    ASSERT_TRUE(j.contains("context"));
    ASSERT_TRUE(j["context"].is_object());
}

TEST(JsonCodecEncodeContextRequestBody, WritesAppNameEnvironmentSessionIdToCorrectFields) {
    Context ctx("unleash-demo2", std::string(), "24876069");
    const std::string body = JsonCodec::encodeContextRequestBody(ctx);
    nlohmann::json j = nlohmann::json::parse(body);

    const auto& c = j["context"];
    EXPECT_EQ(c.value("appName", ""), "unleash-demo2");
    EXPECT_EQ(c.value("sessionId", ""), "24876069");
}

TEST(JsonCodecEncodeContextRequestBody, DonTOmitsSessionIdIfEmpty) {
    Context ctx("app");

    const std::string body = JsonCodec::encodeContextRequestBody(ctx);
    nlohmann::json j = nlohmann::json::parse(body);

    const auto& c = j["context"];
    EXPECT_EQ(c.value("appName", ""), "app");

    if (c.contains("sessionId")) {
        EXPECT_TRUE(c["sessionId"].is_string());
        EXPECT_FALSE(c["sessionId"].get<std::string>().empty());
    }
}

TEST(JsonCodecEncodeContextRequestBody, SerializesOptionalFieldsWhenSet) {
    Context ctx("app", "s1");
    ctx.setUserId("u42").setRemoteAddress("10.0.0.1").setCurrentTime();

    const std::string body = JsonCodec::encodeContextRequestBody(ctx);
    nlohmann::json j = nlohmann::json::parse(body);

    const auto& c = j["context"];
    EXPECT_EQ(c.value("userId", ""), "u42");
    EXPECT_EQ(c.value("remoteAddress", ""), "10.0.0.1");
    EXPECT_FALSE(c.value("currentTime", "").empty());
}

TEST(JsonCodecEncodeContextRequestBody, SerializesPropertiesIfPresent) {
    Context ctx("app", "s1");
    ctx.setProperty("tenant", "acme").setProperty("plan", "pro");

    const std::string body = JsonCodec::encodeContextRequestBody(ctx);
    std::cout << "Body is: " << body << std::endl;
    nlohmann::json j = nlohmann::json::parse(body);

    const auto& c = j["context"];

    ASSERT_TRUE(c.contains("properties"));
    ASSERT_TRUE(c["properties"].is_object());

    EXPECT_EQ(c["properties"].value("tenant", ""), "acme");
    EXPECT_EQ(c["properties"].value("plan", ""), "pro");
}

TEST(JsonCodecEncodeMetricsRequestBody, ProducesValidJsonStructureForEmptyMetricList) {
    MetricList ml;

    const std::string start = "2026-02-05T18:12:21.163Z";
    const std::string stop = "2026-02-05T18:12:51.163Z";
    const std::string app = "unleash-demo2";
    const std::string inst = "browser";

    const std::string body = JsonCodec::encodeMetricsRequestBody(ml, start, stop, app, inst);

    json j = json::parse(body);

    ASSERT_TRUE(j.contains("bucket"));
    ASSERT_TRUE(j["bucket"].is_object());
    EXPECT_EQ(j["bucket"]["start"], start);
    EXPECT_EQ(j["bucket"]["stop"], stop);

    ASSERT_TRUE(j["bucket"].contains("toggles"));
    EXPECT_TRUE(j["bucket"]["toggles"].is_object());
    EXPECT_TRUE(j["bucket"]["toggles"].empty());

    EXPECT_EQ(j["appName"], app);
    EXPECT_EQ(j["instanceId"], inst);
}

TEST(JsonCodecEncodeMetricsRequestBody, EncodesTogglesYesNoAndVariantsCorrectly) {
    MetricList ml;

    for (int i = 0; i < 6; ++i) {
        ml.addVariantMetricData("test-flag", true, "hello");
        ml.addVariantMetricData("test-flag2", true, "hello");
    }

    const std::string start = "2026-02-05T18:12:21.163Z";
    const std::string stop = "2026-02-05T18:12:51.163Z";
    const std::string app = "unleash-demo2";
    const std::string inst = "browser";

    const std::string body = JsonCodec::encodeMetricsRequestBody(ml, start, stop, app, inst);
    json actual = json::parse(body);

    json expected = json::object();
    expected["bucket"] = json::object();
    expected["bucket"]["start"] = start;
    expected["bucket"]["stop"] = stop;

    expected["bucket"]["toggles"] = json::object();

    expected["bucket"]["toggles"]["test-flag"] = json::object();
    expected["bucket"]["toggles"]["test-flag"]["yes"] = 6;
    expected["bucket"]["toggles"]["test-flag"]["no"] = 0;
    expected["bucket"]["toggles"]["test-flag"]["variants"] = json::object();
    expected["bucket"]["toggles"]["test-flag"]["variants"]["hello"] = 6;

    expected["bucket"]["toggles"]["test-flag2"] = json::object();
    expected["bucket"]["toggles"]["test-flag2"]["yes"] = 6;
    expected["bucket"]["toggles"]["test-flag2"]["no"] = 0;
    expected["bucket"]["toggles"]["test-flag2"]["variants"] = json::object();
    expected["bucket"]["toggles"]["test-flag2"]["variants"]["hello"] = 6;

    expected["appName"] = app;
    expected["instanceId"] = inst;

    EXPECT_EQ(actual, expected);
}

TEST(JsonCodecEncodeMetricsRequestBody, HandlesMixedYesNoAndMultipleVariants) {
    MetricList ml;

    ml.addVariantMetricData("flagA", true, "hello");
    ml.addVariantMetricData("flagA", true, "hello");
    ml.addVariantMetricData("flagA", false, "hello");
    ml.addVariantMetricData("flagA", false, "world");
    ml.addVariantMetricData("flagA", false, "world");

    const std::string start = "2026-02-05T18:12:21.163Z";
    const std::string stop = "2026-02-05T18:12:51.163Z";

    const std::string body = JsonCodec::encodeMetricsRequestBody(ml, start, stop, "app", "inst");
    json j = json::parse(body);

    ASSERT_TRUE(j.contains("bucket"));
    ASSERT_TRUE(j["bucket"].contains("toggles"));
    ASSERT_TRUE(j["bucket"]["toggles"].contains("flagA"));

    const auto& flagA = j["bucket"]["toggles"]["flagA"];
    EXPECT_EQ(flagA["yes"], 2);
    EXPECT_EQ(flagA["no"], 3);

    ASSERT_TRUE(flagA.contains("variants"));
    EXPECT_EQ(flagA["variants"]["hello"], 3);
    EXPECT_EQ(flagA["variants"]["world"], 2);
}

TEST(JsonCodecEncodeMetricsRequestBody, HandlesMixedYesNoWithNoVariants) {
    MetricList ml;

    ml.addEnableMetricData("noVariantFlag", true);
    ml.addEnableMetricData("noVariantFlag", true);
    ml.addEnableMetricData("noVariantFlag", false);
    ml.addEnableMetricData("noVariantFlag", false);
    ml.addEnableMetricData("noVariantFlag", false);

    const std::string body =
        JsonCodec::encodeMetricsRequestBody(ml, "2026-02-05T18:12:21.163Z", "2026-02-05T18:12:51.163Z", "app", "inst");

    json j = json::parse(body);

    ASSERT_TRUE(j.contains("bucket"));
    ASSERT_TRUE(j["bucket"].contains("toggles"));
    ASSERT_TRUE(j["bucket"]["toggles"].contains("noVariantFlag"));

    const auto& flag = j["bucket"]["toggles"]["noVariantFlag"];
    EXPECT_EQ(flag["yes"], 2);
    EXPECT_EQ(flag["no"], 3);

    ASSERT_TRUE(flag.contains("variants"));
    EXPECT_TRUE(flag["variants"].is_object());
    EXPECT_TRUE(flag["variants"].empty());
}

TEST(JsonCodecEncodeClientFeaturesResponse, ProducesTogglesArrayAtTopLevel) {
    unleash::ToggleSet::Map m;
    m.emplace("my-flag", unleash::Toggle{"my-flag", true, false});
    unleash::ToggleSet ts{std::move(m)};

    const std::string body = JsonCodec::encodeClientFeaturesResponse(ts);
    json j = json::parse(body);

    ASSERT_TRUE(j.is_object());
    ASSERT_TRUE(j.contains("toggles"));
    ASSERT_TRUE(j["toggles"].is_array());
    EXPECT_EQ(j["toggles"].size(), 1u);
}

TEST(JsonCodecEncodeClientFeaturesResponse, EncodesEnabledAndImpressionDataFields) {
    unleash::ToggleSet::Map m;
    m.emplace("flag-a", unleash::Toggle{"flag-a", true, true});
    m.emplace("flag-b", unleash::Toggle{"flag-b", false, false});
    unleash::ToggleSet ts{std::move(m)};

    const std::string body = JsonCodec::encodeClientFeaturesResponse(ts);
    json j = json::parse(body);

    // Build a lookup by name for order-independent assertions
    // Simplifies the test a little and I think it's okay for the domain
    // to allow these to be unordered since this is a internal lookup structure
    std::unordered_map<std::string, json> byName;
    for (const auto& item : j["toggles"])
        byName[item["name"].get<std::string>()] = item;

    ASSERT_EQ(byName.size(), 2u);

    EXPECT_TRUE(byName["flag-a"]["enabled"].get<bool>());
    EXPECT_TRUE(byName["flag-a"]["impressionData"].get<bool>());

    EXPECT_FALSE(byName["flag-b"]["enabled"].get<bool>());
    EXPECT_FALSE(byName["flag-b"]["impressionData"].get<bool>());
}

TEST(JsonCodecEncodeClientFeaturesResponse, EncodesVariantWithPayload) {
    unleash::Variant v{"control", true, unleash::Variant::Payload{"string", "hello"}};
    unleash::ToggleSet::Map m;
    m.emplace("flag-v", unleash::Toggle{"flag-v", true, false, std::move(v)});
    unleash::ToggleSet ts{std::move(m)};

    const std::string body = JsonCodec::encodeClientFeaturesResponse(ts);
    json j = json::parse(body);

    const json& t = j["toggles"][0];
    ASSERT_TRUE(t.contains("variant"));
    const json& vj = t["variant"];

    EXPECT_EQ(vj["name"].get<std::string>(), "control");
    EXPECT_TRUE(vj["enabled"].get<bool>());
    ASSERT_TRUE(vj.contains("payload"));
    EXPECT_EQ(vj["payload"]["type"].get<std::string>(), "string");
    EXPECT_EQ(vj["payload"]["value"].get<std::string>(), "hello");
}

TEST(JsonCodecEncodeClientFeaturesResponse, OmitsPayloadFieldWhenVariantHasNone) {
    unleash::Variant v{"on", true};
    unleash::ToggleSet::Map m;
    m.emplace("flag-nopayload", unleash::Toggle{"flag-nopayload", true, false, std::move(v)});
    unleash::ToggleSet ts{std::move(m)};

    const std::string body = JsonCodec::encodeClientFeaturesResponse(ts);
    json j = json::parse(body);

    const json& vj = j["toggles"][0]["variant"];
    EXPECT_FALSE(vj.contains("payload"));
}

TEST(JsonCodecEncodeClientFeaturesResponse, EmptyToggleSetProducesEmptyArray) {
    unleash::ToggleSet ts;

    const std::string body = JsonCodec::encodeClientFeaturesResponse(ts);
    json j = json::parse(body);

    ASSERT_TRUE(j.contains("toggles"));
    EXPECT_TRUE(j["toggles"].is_array());
    EXPECT_TRUE(j["toggles"].empty());
}

TEST(JsonCodecRoundTrip, DecodeEncodeDecodeProducesSameToggleSet) {
    const std::string original = R"json({
      "toggles": [
        {
          "name": "flag-full",
          "enabled": true,
          "impressionData": true,
          "variant": {
            "name": "variant-a",
            "enabled": true,
            "payload": { "type": "string", "value": "abc" }
          }
        },
        {
          "name": "flag-no-payload",
          "enabled": true,
          "impressionData": false,
          "variant": { "name": "on", "enabled": true }
        },
        {
          "name": "flag-disabled",
          "enabled": false,
          "impressionData": false,
          "variant": { "name": "disabled", "enabled": false }
        }
      ]
    })json";

    auto first = JsonCodec::decodeClientFeaturesResponse(original);
    ASSERT_TRUE(first.has_value());
    const std::string encoded = JsonCodec::encodeClientFeaturesResponse(first.value());
    auto second = JsonCodec::decodeClientFeaturesResponse(encoded);
    ASSERT_TRUE(second.has_value());

    ASSERT_EQ(first->size(), second->size());

    for (const auto& [name, toggle] : first->toggles()) {
        ASSERT_TRUE(second->contains(name)) << "missing toggle: " << name;
        EXPECT_EQ(second->isEnabled(name), toggle.enabled()) << name;
        EXPECT_EQ(second->impressionData(name), toggle.impressionData()) << name;

        const unleash::Variant v1 = first->getVariant(name);
        const unleash::Variant v2 = second->getVariant(name);
        EXPECT_EQ(v1, v2) << "variant mismatch for: " << name;
    }
}
