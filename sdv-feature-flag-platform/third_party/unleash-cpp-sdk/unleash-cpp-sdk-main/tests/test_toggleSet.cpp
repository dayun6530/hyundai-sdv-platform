// tests/test_toggle_set.cpp
#include <gtest/gtest.h>

#include "unleash/Domain/toggleSet.hpp"
#include "unleash/Domain/toggle.hpp"
#include "unleash/Domain/variant.hpp"

using unleash::Toggle;
using unleash::ToggleSet;
using unleash::Variant;

static Toggle makeToggle(std::string name, bool enabled, bool impression, std::string variantName = "disabled",
                         bool variantEnabled = false) {
    Toggle t(std::move(name), enabled, impression, Variant(std::move(variantName), variantEnabled));
    return t;
}

TEST(ToggleSetTest, DefaultConstructedIsEmpty) {
    ToggleSet set;
    EXPECT_EQ(set.size(), 0u);

    EXPECT_FALSE(set.contains("missing"));

    EXPECT_FALSE(set.isEnabled("missing"));
    EXPECT_EQ(set.getVariant("missing"), Variant::disabledFactory());
    EXPECT_FALSE(set.impressionData("missing"));
}

TEST(ToggleSetTest, ConstructFromVector_BuildsLookup) {
    std::vector<Toggle> toggles;
    toggles.emplace_back(makeToggle("A", true, false, "red", true));
    toggles.emplace_back(makeToggle("B", false, true, "blue", true));

    ToggleSet set(toggles);

    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains("A"));
    EXPECT_TRUE(set.contains("B"));
    EXPECT_FALSE(set.contains("C"));

    EXPECT_TRUE(set.isEnabled("A"));
    EXPECT_FALSE(set.impressionData("A"));
    EXPECT_EQ(set.getVariant("A").name(), "red");
    EXPECT_TRUE(set.getVariant("A").enabled());

    EXPECT_FALSE(set.isEnabled("B"));
    EXPECT_TRUE(set.impressionData("B"));
    EXPECT_EQ(set.getVariant("B").name(), "blue");
    EXPECT_TRUE(set.getVariant("B").enabled());
}

TEST(ToggleSetTest, ConstructFromMoveVector_BuildsLookup) {
    std::vector<Toggle> toggles;
    toggles.emplace_back(makeToggle("X", true, true, "v1", true));
    toggles.emplace_back(makeToggle("Y", false, false, "v2", true));

    ToggleSet set(std::move(toggles));

    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains("X"));
    EXPECT_TRUE(set.contains("Y"));

    EXPECT_TRUE(set.isEnabled("X"));
    EXPECT_TRUE(set.impressionData("X"));
    EXPECT_EQ(set.getVariant("X").name(), "v1");

    EXPECT_FALSE(set.isEnabled("Y"));
    EXPECT_FALSE(set.impressionData("Y"));
    EXPECT_EQ(set.getVariant("Y").name(), "v2");
}

TEST(ToggleSetTest, MissingToggleReturnsDefaultsEvenWhenNotEmpty) {
    ToggleSet set(std::vector<Toggle>{makeToggle("exists", true, true, "x", true)});

    EXPECT_FALSE(set.contains("missing"));
    EXPECT_FALSE(set.isEnabled("missing"));
    EXPECT_FALSE(set.impressionData("missing"));
    EXPECT_EQ(set.getVariant("missing"), Variant::disabledFactory());
}

TEST(ToggleSetTest, DuplicateNames_FirstOneWins) {
    std::vector<Toggle> toggles;
    toggles.emplace_back(makeToggle("dup", false, false, "old", false));
    toggles.emplace_back(makeToggle("dup", true, true, "new", true));

    ToggleSet set(std::move(toggles));

    // Should collapse duplicates to one entry
    EXPECT_EQ(set.size(), 1u);
    EXPECT_TRUE(set.contains("dup"));

    // Last wins:
    EXPECT_FALSE(set.isEnabled("dup"));
    EXPECT_FALSE(set.impressionData("dup"));
    EXPECT_NE(set.getVariant("dup").name(), "new");
    EXPECT_FALSE(set.getVariant("dup").enabled());
}
