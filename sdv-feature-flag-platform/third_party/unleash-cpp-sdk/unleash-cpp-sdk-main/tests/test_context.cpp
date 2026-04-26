#include <gtest/gtest.h>
#include "unleash/Domain/context.hpp"
#include "unleash/Utils/utils.hpp"

using unleash::Context;
using unleash::MutableContext;

TEST(ContextTest, ConstructorSetsRequiredFields) {
    Context ctx("myApp", "environment", "123568941");
    EXPECT_EQ(ctx.getAppName(), "myApp");
    EXPECT_EQ(ctx.getEnvironment(), "environment");
    EXPECT_EQ(ctx.getSessionId(), "123568941");
}

TEST(ContextTest, ConstructorSetNoField) {
    Context ctx{};
    EXPECT_EQ(ctx.getAppName(), std::string(utils::defaultAppName));
    EXPECT_FALSE(ctx.getEnvironment().has_value());
    EXPECT_FALSE(ctx.getSessionId().empty());
}

TEST(ContextTest, OptionalFieldsAreEmptyByDefault) {
    Context ctx("myApp");
    auto sessionId = ctx.getSessionId();

    EXPECT_FALSE(ctx.getUserId().has_value());
    EXPECT_FALSE(ctx.getEnvironment().has_value());
    EXPECT_FALSE(ctx.getRemoteAddress().has_value());
    EXPECT_FALSE(ctx.getCurrentTime().has_value());
    // verify that _sessionId is not empty:
    EXPECT_FALSE(sessionId.empty());
}

TEST(ContextTest, SettersSetAndEmptyResets) {
    MutableContext mCtx{};

    mCtx.setUserId("u1");
    EXPECT_TRUE(mCtx.getUserId().has_value());
    EXPECT_EQ(mCtx.getUserId().value(), "u1");

    mCtx.setUserId(""); // should reset
    EXPECT_FALSE(mCtx.getUserId().has_value());
    mCtx.setRemoteAddress("10.0.0.1");
    ASSERT_TRUE(mCtx.getRemoteAddress().has_value());
    EXPECT_EQ(mCtx.getRemoteAddress().value(), "10.0.0.1");

    mCtx.setRemoteAddress("");
    EXPECT_FALSE(mCtx.getRemoteAddress().has_value());
}

TEST(ContextTest, SetMutableFieldsFromContextClass) {
    Context ctx{};

    ctx.setUserId("u1");
    EXPECT_TRUE(ctx.getUserId().has_value());
    EXPECT_EQ(ctx.getUserId().value(), "u1");

    ctx.setUserId(""); // should reset
    EXPECT_FALSE(ctx.getUserId().has_value());
    ctx.setRemoteAddress("10.0.0.1");
    ASSERT_TRUE(ctx.getRemoteAddress().has_value());
    EXPECT_EQ(ctx.getRemoteAddress().value(), "10.0.0.1");

    ctx.setRemoteAddress("");
    EXPECT_FALSE(ctx.getRemoteAddress().has_value());
}

TEST(ContextTest, SetPropertyAddsAndUpdates) {
    MutableContext mCtx{};

    mCtx.setProperty("tenant", "acme");
    ASSERT_EQ(mCtx.getProperties().size(), 1u);
    EXPECT_EQ(mCtx.getProperties()[0].first, "tenant");
    EXPECT_EQ(mCtx.getProperties()[0].second, "acme");

    mCtx.setProperty("tenant", "acme2");
    ASSERT_EQ(mCtx.getProperties().size(), 1u);
    EXPECT_EQ(mCtx.getProperties()[0].second, "acme2");
}

TEST(ContextTest, SetPropertyIgnoresEmptyAndReservedKeys) {
    MutableContext mCtx{};

    mCtx.setProperty("", "x");
    EXPECT_TRUE(mCtx.getProperties().empty());

    mCtx.setProperty("userId", "x");
    mCtx.setProperty("appName", "x");
    mCtx.setProperty("currentTime", "x");
    EXPECT_TRUE(mCtx.getProperties().empty());
}

TEST(ContextTest, SetPropertyFromContext) {
    Context ctx("myApp", "developpement");

    ctx.setProperty("", "x");
    ctx.setProperty("toto1", "1");
    EXPECT_FALSE(ctx.getProperties().empty());
    ASSERT_EQ(ctx.getProperties().size(), 1u);
}

TEST(ContextTest, SetCurrentTimeProperty) {
    Context ctx("myApp");
    EXPECT_FALSE(ctx.getCurrentTime().has_value());
    MutableContext mCtx{};
    mCtx.setCurrentTime();
    ctx.updateMutableContext(mCtx);
    EXPECT_TRUE(ctx.getCurrentTime().has_value());
}
