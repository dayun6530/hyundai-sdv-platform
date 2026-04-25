package com.sdv.killcontrol.sdv.model;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonIgnoreProperties(ignoreUnknown = true)
public record VehicleTelemetry(
        @JsonProperty("vehicle_id")
        @JsonAlias({"vehicleId"})
        String vehicleId,

        @JsonProperty("request_id")
        @JsonAlias({"requestId"})
        String requestId,

        String command,
        @JsonProperty("target_feature")
        @JsonAlias({"targetFeature"})
        String targetFeature,

        String version,
        String status,
        String detail,
        @JsonProperty("ts_ms")
        @JsonAlias({"tsMs"})
        Long tsMs
) {
}

