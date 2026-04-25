package com.sdv.killcontrol.sdv.store;

import java.time.Instant;

public record CommandStatus(
        String vehicleId,
        String requestId,
        String command,
        String targetFeature,
        String version,
        String status,
        String detail,
        Instant lastUpdated
) {
}

