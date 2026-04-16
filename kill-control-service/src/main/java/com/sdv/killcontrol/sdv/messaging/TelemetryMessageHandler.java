package com.sdv.killcontrol.sdv.messaging;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sdv.killcontrol.sdv.model.VehicleTelemetry;
import com.sdv.killcontrol.sdv.store.CommandStatusStore;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.messaging.MessageHeaders;
import org.springframework.stereotype.Component;

import java.nio.charset.StandardCharsets;

@Component
public class TelemetryMessageHandler {
    private static final Logger log = LoggerFactory.getLogger(TelemetryMessageHandler.class);

    private final CommandStatusStore store;

    public TelemetryMessageHandler(CommandStatusStore store) {
        this.store = store;
    }

    public void handleTelemetry(MessageHeaders headers, byte[] payloadBytes, ObjectMapper objectMapper) {
        String payload = new String(payloadBytes, StandardCharsets.UTF_8);
        try {
            VehicleTelemetry telemetry = objectMapper.readValue(payload, VehicleTelemetry.class);
            log.info("telemetry rx: vehicleId={} requestId={} status={} detail={}",
                    telemetry.vehicleId(), telemetry.requestId(), telemetry.status(), telemetry.detail());
            store.updateFromTelemetry(telemetry);
        } catch (Exception e) {
            log.warn("telemetry parse failed: payload={} err={}", payload, e.toString());
        }
    }
}

