#include <gtest/gtest.h>

#include "unleash/Metrics/metricList.hpp"

#include <string>

using namespace unleash;

TEST(MetricListTest, DefaultConstructedIsEmpty) {
    MetricList ml;
    EXPECT_TRUE(ml.getList().empty());
}

TEST(MetricListTest, AddMetricData_FirstInsertCreatesToggleAndCountsFirstEvent) {
    MetricList ml;

    ml.addVariantMetricData("flagA", true, "variant1");

    const auto& m = ml.getList();
    ASSERT_EQ(m.size(), 1u);

    auto it = m.find("flagA");
    ASSERT_NE(it, m.end());

    EXPECT_EQ(it->second.getToggleName(), "flagA");

    EXPECT_EQ(it->second.getYesCount(), 1u);
    EXPECT_EQ(it->second.getNoCount(), 0u);

    const auto& stats = it->second.getVariantStats();
    ASSERT_EQ(stats.size(), 1u);
    EXPECT_EQ(stats.at("variant1"), 1u);
}

TEST(MetricListTest, AddMetricData_SecondCallUpdatesExistingToggle) {
    MetricList ml;

    ml.addVariantMetricData("flagA", true, "variant1");

    ml.addVariantMetricData("flagA", true, "variant1");
    ml.addVariantMetricData("flagA", false, "variant1");
    ml.addVariantMetricData("flagA", false, "variant2");

    const auto& m = ml.getList();
    ASSERT_EQ(m.size(), 1u);

    auto it = m.find("flagA");
    ASSERT_NE(it, m.end());

    EXPECT_EQ(it->second.getToggleName(), "flagA");
    EXPECT_EQ(it->second.getYesCount(), 2u);
    EXPECT_EQ(it->second.getNoCount(), 2u);

    const auto& stats = it->second.getVariantStats();
    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats.at("variant1"), 3u);
    EXPECT_EQ(stats.at("variant2"), 1u);
}

TEST(MetricListTest, AddMetricData_MultipleTogglesAreTrackedIndependently) {
    MetricList ml;

    ml.addVariantMetricData("flagA", true, "v1");
    ml.addVariantMetricData("flagB", false, "blue");
    ml.addVariantMetricData("flagB", true, "blue");
    ml.addVariantMetricData("flagA", false, "v2");

    const auto& m = ml.getList();
    ASSERT_EQ(m.size(), 2u);

    {
        auto it = m.find("flagA");
        ASSERT_NE(it, m.end());
        EXPECT_EQ(it->second.getToggleName(), "flagA");
        EXPECT_EQ(it->second.getYesCount(), 1u);
        EXPECT_EQ(it->second.getNoCount(), 1u);

        const auto& stats = it->second.getVariantStats();
        ASSERT_EQ(stats.size(), 2u);
        EXPECT_EQ(stats.at("v1"), 1u);
        EXPECT_EQ(stats.at("v2"), 1u);
    }

    {
        auto it = m.find("flagB");
        ASSERT_NE(it, m.end());
        EXPECT_EQ(it->second.getToggleName(), "flagB");
        EXPECT_EQ(it->second.getYesCount(), 1u);
        EXPECT_EQ(it->second.getNoCount(), 1u);

        const auto& stats = it->second.getVariantStats();
        ASSERT_EQ(stats.size(), 1u);
        EXPECT_EQ(stats.at("blue"), 2u);
    }
}
