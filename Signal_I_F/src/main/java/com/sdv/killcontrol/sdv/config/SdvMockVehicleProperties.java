package com.sdv.killcontrol.sdv.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "sdv.mock-vehicle")
public record SdvMockVehicleProperties(
        boolean enabled,
        String vehicleId
) {
}

