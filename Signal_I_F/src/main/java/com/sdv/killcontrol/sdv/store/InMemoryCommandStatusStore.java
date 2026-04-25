package com.sdv.killcontrol.sdv.store;

import com.sdv.killcontrol.sdv.model.VehicleTelemetry;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class InMemoryCommandStatusStore implements CommandStatusStore {
    private static final Logger log = LoggerFactory.getLogger(InMemoryCommandStatusStore.class);

    private final ConcurrentHashMap<String, CommandStatus> latestByKey = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, CompletableFuture<CommandStatus>> pendingByKey = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, CompletableFuture<CommandStatus>> pendingByAltKey = new ConcurrentHashMap<>();

    @Override
    public CompletableFuture<CommandStatus> registerPending(String vehicleId,
                                                            String requestId,
                                                            String command,
                                                            String targetFeature,
                                                            String version) {
        String key = key(vehicleId, requestId);
        CompletableFuture<CommandStatus> future = new CompletableFuture<>();
        pendingByKey.put(key, future);
        pendingByAltKey.put(altKey(vehicleId, command, targetFeature, version), future);

        latestByKey.put(key, new CommandStatus(
                vehicleId,
                requestId,
                command,
                targetFeature,
                version,
                "PENDING",
                "Queued",
                Instant.now()
        ));
        return future;
    }

    @Override
    public void updateFromTelemetry(VehicleTelemetry telemetry) {
        String vehicleId = telemetry.vehicleId();
        String requestId = telemetry.requestId();
        if (vehicleId == null || vehicleId.isBlank()) return;

        String key = (requestId == null || requestId.isBlank())
                ? null
                : key(vehicleId, requestId);
        CommandStatus status = new CommandStatus(
                vehicleId,
                (requestId == null) ? "" : requestId,
                telemetry.command(),
                telemetry.targetFeature(),
                telemetry.version(),
                telemetry.status(),
                telemetry.detail(),
                Instant.now()
        );

        if (key != null) {
            latestByKey.put(key, status);
            CompletableFuture<CommandStatus> pending = pendingByKey.remove(key);
            pendingByAltKey.remove(altKey(vehicleId, telemetry.command(), telemetry.targetFeature(), telemetry.version()));
            if (pending != null && !pending.isDone()) {
                pending.complete(status);
            }
            return;
        }

        String aKey = altKey(vehicleId, telemetry.command(), telemetry.targetFeature(), telemetry.version());
        CompletableFuture<CommandStatus> pending = pendingByAltKey.remove(aKey);
        if (pending != null && !pending.isDone()) {
            pending.complete(status);
        } else {
            log.debug("Telemetry not correlated (no requestId). vehicleId={} command={} targetFeature={} version={}",
                    vehicleId, telemetry.command(), telemetry.targetFeature(), telemetry.version());
        }
    }

    @Override
    public Optional<CommandStatus> findLatest(String vehicleId, String requestId) {
        return Optional.ofNullable(latestByKey.get(key(vehicleId, requestId)));
    }

    private static String key(String vehicleId, String requestId) {
        return vehicleId + ":" + requestId;
    }

    private static String altKey(String vehicleId, String command, String targetFeature, String version) {
        return vehicleId + ":" + String.valueOf(command) + ":" + String.valueOf(targetFeature) + ":" + String.valueOf(version);
    }
}

