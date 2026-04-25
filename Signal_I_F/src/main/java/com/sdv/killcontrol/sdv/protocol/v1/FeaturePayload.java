package com.sdv.killcontrol.sdv.protocol.v1;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;

public record FeaturePayload(
        @JsonProperty("feature_name")
        @JsonAlias({"featureName"})
        String featureName,

        String version,

        String action,

        @JsonProperty("binary_path")
        @JsonAlias({"binaryPath"})
        String binaryPath,

        @JsonProperty("current_feature")
        @JsonAlias({"currentFeature"})
        String currentFeature
) {
}

