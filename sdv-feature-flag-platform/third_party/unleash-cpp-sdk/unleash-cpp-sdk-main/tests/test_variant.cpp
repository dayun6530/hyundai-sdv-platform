#include <gtest/gtest.h>
#include "unleash/Domain/variant.hpp"

using unleash::Variant;

TEST(VariantTest, DefaultConstructedIsDisabledEmptyName) {
    Variant v;
    EXPECT_FALSE(v.enabled());
    EXPECT_TRUE(v.name().empty());
    EXPECT_FALSE(v.hasPayload());
}

TEST(VariantTest, DisabledFactoryIsConsistent) {
    Variant v = Variant::disabledFactory();
    EXPECT_EQ(v.name(), "disabled");
    EXPECT_FALSE(v.enabled());
    EXPECT_FALSE(v.hasPayload());
}

TEST(VariantTest, SetPayloadValueIsCorrect) {
    Variant a("Test", true, Variant::Payload{"string", "x"});

    const std::optional<Variant::Payload> p = a.payload();
    EXPECT_TRUE(p.has_value());
    EXPECT_EQ(p.value().type(), "string");
    EXPECT_EQ(p.value().value(), "x");
}

TEST(VariantTest, Equality) {
    Variant a("A", true, Variant::Payload{"string", "x"});
    Variant b("A", true, Variant::Payload{"string", "x"});
    Variant c("A", true, Variant::Payload{"string", "y"});
    Variant d("A", false);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}