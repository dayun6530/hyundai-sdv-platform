package com.sdv.killcontrol.sdv.model;

import com.fasterxml.jackson.annotation.JsonInclude;

import java.util.Map;

@JsonInclude(JsonInclude.Include.NON_NULL)
public record VehicleCommandRequest(
        String requestId,
        String command,
        String targetFeature,
        String version,
        String payloadUrl,
        Map<String, Object> shadow
) {
}

