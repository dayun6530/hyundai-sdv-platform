#include <gtest/gtest.h>

#include "unleash/Metrics/metricToggle.hpp"

#include <string>

using namespace unleash;

TEST(MetricToggleTest, Constructor_InitializesCountsAndVariantStatsCorrectly) {
    MetricToggle mt("flagA", true, "variant1");

    EXPECT_EQ(mt.getToggleName(), "flagA");
    EXPECT_EQ(mt.getYesCount(), 1u);
    EXPECT_EQ(mt.getNoCount(), 0u);

    const auto& stats = mt.getVariantStats();
    ASSERT_EQ(stats.size(), 1u);
    ASSERT_TRUE(stats.find("variant1") != stats.end());
    EXPECT_EQ(stats.at("variant1"), 1u); // must be 1, not 2
}

TEST(MetricToggleTest, UpdateMetric_IncrementsSameVariantAndYesNoCounters) {
    MetricToggle mt("flagA", true, "variant1");

    mt.updateVariantMetric(true, "variant1");
    mt.updateVariantMetric(false, "variant1");
    mt.updateVariantMetric(false, "variant1");

    EXPECT_EQ(mt.getYesCount(), 2u);
    EXPECT_EQ(mt.getNoCount(), 2u);

    const auto& stats = mt.getVariantStats();
    ASSERT_EQ(stats.size(), 1u);
    EXPECT_EQ(stats.at("variant1"), 4u); // 1 from ctor + 3 updates
}

TEST(MetricToggleTest, UpdateMetric_TracksMultipleVariantsSeparately) {
    MetricToggle mt("flagA", false, "blue");

    mt.updateVariantMetric(true, "blue");   // blue++
    mt.updateVariantMetric(true, "green");  // green++
    mt.updateVariantMetric(false, "green"); // green++
    mt.updateVariantMetric(false, "red");   // red++

    EXPECT_EQ(mt.getYesCount(), 2u);
    EXPECT_EQ(mt.getNoCount(), 3u);
    mt.updateEnableMetric(false);
    EXPECT_EQ(mt.getNoCount(), 4u);

    const auto& stats = mt.getVariantStats();
    ASSERT_EQ(stats.size(), 3u);

    EXPECT_EQ(stats.at("blue"), 2u);
    EXPECT_EQ(stats.at("green"), 2u);
    EXPECT_EQ(stats.at("red"), 1u);
}
