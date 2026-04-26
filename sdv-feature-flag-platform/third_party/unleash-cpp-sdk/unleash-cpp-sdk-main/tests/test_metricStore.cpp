#include <gtest/gtest.h>

#include "unleash/Metrics/metricStore.hpp"
#include "unleash/Configuration/clientConfig.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <thread>
#include <vector>
#include <string>

using nlohmann::json;
using namespace unleash;

namespace {

static ClientConfig makeCfgForMetrics(const std::string& appName, const std::string& instanceId) {
    ClientConfig cfg("http://127.0.0.1:1", "dummy-key", appName);

    cfg.setInstanceId(instanceId);

    return cfg;
}

static json parsePayload(const std::string& s) {
    return json::parse(s);
}

} // namespace

TEST(MetricsStoreTest, InitiallyEmpty_StartTimestampSet_ToJsonReturnsNullopt) {
    auto cfg = makeCfgForMetrics("unleash-demo2", "browser");
    MetricsStore store(cfg);

    EXPECT_TRUE(store.empty());

    const auto ts = store.startTimestampMs();
    EXPECT_GT(ts, 0);

    auto payload = store.toJsonMetricsPayload();
    EXPECT_FALSE(payload.has_value());
}

TEST(MetricsStoreTest, AddMetricData_MakesNonEmpty_AndSnapshotReflectsMetrics) {
    auto cfg = makeCfgForMetrics("unleash-demo2", "browser");
    MetricsStore store(cfg);

    store.addVariantMetric("test-flag", true, "hello");
    store.addVariantMetric("test-flag", true, "hello");
    store.addVariantMetric("test-flag", false, "hello");

    EXPECT_FALSE(store.empty());

    MetricList snap = store.snapshot();
    const auto& m = snap.getList();

    ASSERT_EQ(m.size(), 1u);
    auto it = m.find("test-flag");
    ASSERT_NE(it, m.end());

    EXPECT_EQ(it->second.getToggleName(), "test-flag");
    EXPECT_EQ(it->second.getYesCount(), 2u);
    EXPECT_EQ(it->second.getNoCount(), 1u);
    EXPECT_EQ(it->second.getVariantStats().at("hello"), 3u);
}

TEST(MetricsStoreTest, Reset_EmptiesAndUpdatesStartTimestamp) {
    auto cfg = makeCfgForMetrics("unleash-demo2", "browser");
    MetricsStore store(cfg);

    store.addVariantMetric("test-flag", true, "hello");
    ASSERT_FALSE(store.empty());

    const auto before = store.startTimestampMs();
    store.reset();

    EXPECT_TRUE(store.empty());
    const auto after = store.startTimestampMs();

    EXPECT_GE(after, before);
}

TEST(MetricsStoreTest, toJsonMetricsPayload_EncodesExpectedShapeAndCounts) {
    auto cfg = makeCfgForMetrics("unleash-demo2", "browser");
    MetricsStore store(cfg);

    for (int i = 0; i < 6; ++i) {
        store.addVariantMetric("test-flag", true, "hello");
        store.addVariantMetric("test-flag2", true, "hello");
    }

    auto payloadOpt = store.toJsonMetricsPayload();
    ASSERT_TRUE(payloadOpt.has_value());

    json j = parsePayload(*payloadOpt);

    ASSERT_TRUE(j.contains("bucket"));
    ASSERT_TRUE(j.contains("appName"));
    ASSERT_TRUE(j.contains("instanceId"));

    EXPECT_EQ(j["appName"], "unleash-demo2");
    EXPECT_EQ(j["instanceId"], "browser");

    const auto& b = j["bucket"];
    ASSERT_TRUE(b.is_object());
    ASSERT_TRUE(b.contains("start"));
    ASSERT_TRUE(b.contains("stop"));
    ASSERT_TRUE(b.contains("toggles"));

    EXPECT_TRUE(b["start"].is_string());
    EXPECT_TRUE(b["stop"].is_string());

    const auto& toggles = b["toggles"];
    ASSERT_TRUE(toggles.is_object());

    ASSERT_TRUE(toggles.contains("test-flag"));
    ASSERT_TRUE(toggles.contains("test-flag2"));

    {
        const auto& t = toggles["test-flag"];
        EXPECT_EQ(t["yes"], 6);
        EXPECT_EQ(t["no"], 0);
        ASSERT_TRUE(t.contains("variants"));
        EXPECT_EQ(t["variants"]["hello"], 6);
    }

    {
        const auto& t = toggles["test-flag2"];
        EXPECT_EQ(t["yes"], 6);
        EXPECT_EQ(t["no"], 0);
        ASSERT_TRUE(t.contains("variants"));
        EXPECT_EQ(t["variants"]["hello"], 6);
    }
}

TEST(MetricsStoreTest, ThreadSafety_ManyThreadsUpdatingTotalsAreCorrect) {
    auto cfg = makeCfgForMetrics("unleash-demo2", "browser");
    MetricsStore store(cfg);

    constexpr int kThreads = 8;
    constexpr int kIters = 2000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&store, &kIters, t] {
            for (int i = 0; i < kIters; ++i) {
                const bool yes = ((i + t) % 2) == 0;
                if ((i % 2) == 0)
                    store.addVariantMetric("flagA", yes, "v1");
                else
                    store.addVariantMetric("flagB", yes, "v2");
            }
        });
    }

    for (auto& th : threads)
        th.join();

    MetricList snap = store.snapshot();
    const auto& m = snap.getList();

    ASSERT_EQ(m.size(), 2u);
    ASSERT_TRUE(m.find("flagA") != m.end());
    ASSERT_TRUE(m.find("flagB") != m.end());

    const unsigned totalEvents = kThreads * kIters;

    const unsigned expectedPerFlag = totalEvents / 2;

    {
        const auto& A = m.at("flagA");
        EXPECT_EQ(A.getYesCount() + A.getNoCount(), expectedPerFlag);
        EXPECT_EQ(A.getVariantStats().at("v1"), expectedPerFlag);
    }
    {
        const auto& B = m.at("flagB");
        EXPECT_EQ(B.getYesCount() + B.getNoCount(), expectedPerFlag);
        EXPECT_EQ(B.getVariantStats().at("v2"), expectedPerFlag);
    }
}
