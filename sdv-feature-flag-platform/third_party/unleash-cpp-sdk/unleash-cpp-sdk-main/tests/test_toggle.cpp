#include <gtest/gtest.h>

#include "unleash/Domain/toggle.hpp"
#include "unleash/Domain/variant.hpp"

using unleash::Toggle;
using unleash::Variant;

TEST(ToggleTest, MinimalConstructorDefaultsToDisabled) {
    Toggle t("flagA");

    EXPECT_EQ(t.name(), "flagA");
    EXPECT_FALSE(t.enabled());
    EXPECT_FALSE(t.impressionData());

    EXPECT_EQ(t.variant(), Variant::disabledFactory());
}

TEST(ToggleTest, FullConstructorSetsAllFields) {
    Variant v("red", true, Variant::Payload{"string", "hello"});
    Toggle t("flagB", true, true, v);

    EXPECT_EQ(t.name(), "flagB");
    EXPECT_TRUE(t.enabled());
    EXPECT_TRUE(t.impressionData());

    EXPECT_EQ(t.variant(), v);
    ASSERT_TRUE(t.variant().hasPayload());
    EXPECT_EQ(t.variant().payload()->type(), "string");
    EXPECT_EQ(t.variant().payload()->value(), "hello");
}

TEST(ToggleTest, FullConstructorSetsWithDisabledVariant) {
    Toggle t("flagB", true, true);

    EXPECT_EQ(t.name(), "flagB");
    EXPECT_TRUE(t.enabled());
    EXPECT_TRUE(t.impressionData());

    EXPECT_EQ(t.variant(), Variant::disabledFactory());
    ASSERT_FALSE(t.variant().hasPayload());
}