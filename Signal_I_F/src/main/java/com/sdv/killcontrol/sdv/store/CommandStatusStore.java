package com.sdv.killcontrol.sdv.store;

import com.sdv.killcontrol.sdv.model.VehicleTelemetry;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;

public interface CommandStatusStore {
    CompletableFuture<CommandStatus> registerPending(String vehicleId,
                                                     String requestId,
                                                     String command,
                                                     String targetFeature,
                                                     String version);

    void updateFromTelemetry(VehicleTelemetry telemetry);

    Optional<CommandStatus> findLatest(String vehicleId, String requestId);
}

