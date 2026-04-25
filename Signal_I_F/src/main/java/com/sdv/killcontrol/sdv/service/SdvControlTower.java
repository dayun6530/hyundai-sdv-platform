package com.sdv.killcontrol.sdv.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sdv.killcontrol.sdv.messaging.SdvMqttGateway;
import com.sdv.killcontrol.sdv.model.VehicleCommandRequest;
import com.sdv.killcontrol.sdv.protocol.v1.MqttMessageV1;
import com.sdv.killcontrol.sdv.protocol.v1.MqttV1Codec;
import com.sdv.killcontrol.sdv.store.CommandStatus;
import com.sdv.killcontrol.sdv.store.CommandStatusStore;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

@Service
public class SdvControlTower {
    private static final Logger log = LoggerFactory.getLogger(SdvControlTower.class);

    private final SdvMqttGateway mqttGateway;
    private final ObjectMapper objectMapper;
    private final CommandStatusStore statusStore;

    public SdvControlTower(SdvMqttGateway mqttGateway, ObjectMapper objectMapper, CommandStatusStore statusStore) {
        this.mqttGateway = mqttGateway;
        this.objectMapper = objectMapper;
        this.statusStore = statusStore;
    }

    public CompletableFuture<CommandStatus> sendKillSwitch(String vehicleId, String targetFeature, Duration timeout) {
        MqttMessageV1 msg = MqttV1Codec.newKillSwitchMessage(vehicleId, targetFeature);
        return publishAndAwaitV1(vehicleId, msg, timeout);
    }

    public CompletableFuture<CommandStatus> sendDeploy(String vehicleId,
                                                       String targetFeature,
                                                       String version,
                                                       String payloadUrl,
                                                       Duration timeout) {
        // In v1, the payload uses "binary_path". Keep using payloadUrl as-is for compatibility.
        MqttMessageV1 msg = MqttV1Codec.newDeployMessage(vehicleId, targetFeature, version, "SHADOW_RUN", payloadUrl);
        return publishAndAwaitV1(vehicleId, msg, timeout);
    }

    public CompletableFuture<CommandStatus> sendShadowMode(String vehicleId,
                                                           String runningFeature,
                                                           String shadowFeature,
                                                           String shadowVersion,
                                                           Duration timeout) {
        // Shadow mode is represented as DEPLOY_FEATURE with action=SHADOW_RUN.
        // current_feature is used to tell the vehicle what is currently running.
        MqttMessageV1 msg = MqttV1Codec.newShadowRunMessage(vehicleId, runningFeature, shadowFeature, shadowVersion, "");
        return publishAndAwaitV1(vehicleId, msg, timeout);
    }

    private CompletableFuture<CommandStatus> publishAndAwaitV1(String vehicleId,
                                                               MqttMessageV1 message,
                                                               Duration timeout) {
        Objects.requireNonNull(vehicleId, "vehicleId");
        if (vehicleId.isBlank()) throw new IllegalArgumentException("vehicleId must not be blank");
        Objects.requireNonNull(message, "message");

        String topic = "/sdv/vehicle/" + vehicleId + "/control";
        String requestId = message.header().msgId();

        CompletableFuture<CommandStatus> future =
                statusStore.registerPending(vehicleId, requestId, message.command(),
                        message.payload() == null ? "" : message.payload().featureName(),
                        message.payload() == null ? "" : message.payload().version());

        try {
            String payload = MqttV1Codec.encode(objectMapper, message);
            log.info("command tx(v1): topic={} vehicleId={} msgId={} command={} feature={}",
                    topic, vehicleId, requestId, message.command(),
                    (message.payload() == null ? "" : message.payload().featureName()));
            mqttGateway.publish(topic, payload);
        } catch (Exception e) {
            future.completeExceptionally(e);
        }

        Duration effectiveTimeout = (timeout == null) ? Duration.ofSeconds(5) : timeout;
        return future.orTimeout(effectiveTimeout.toMillis(), TimeUnit.MILLISECONDS);
    }

    private CompletableFuture<CommandStatus> publishAndAwait(String vehicleId,
                                                             VehicleCommandRequest request,
                                                             Duration timeout) {
        Objects.requireNonNull(vehicleId, "vehicleId");
        if (vehicleId.isBlank()) throw new IllegalArgumentException("vehicleId must not be blank");
        Objects.requireNonNull(request, "request");

        String topic = "/sdv/vehicle/" + vehicleId + "/control";

        CompletableFuture<CommandStatus> future =
                statusStore.registerPending(vehicleId, request.requestId(), request.command(),
                        request.targetFeature(), request.version());

        try {
            String payload = objectMapper.writeValueAsString(Map.of(
                    "request_id", request.requestId(),
                    "command", request.command(),
                    "target_feature", request.targetFeature(),
                    "version", request.version(),
                    "payload_url", request.payloadUrl(),
                    "shadow", request.shadow()
            ));

            log.info("command tx: topic={} vehicleId={} requestId={} command={} targetFeature={}",
                    topic, vehicleId, request.requestId(), request.command(), request.targetFeature());
            mqttGateway.publish(topic, payload);
        } catch (Exception e) {
            future.completeExceptionally(e);
        }

        Duration effectiveTimeout = (timeout == null) ? Duration.ofSeconds(5) : timeout;
        return future.orTimeout(effectiveTimeout.toMillis(), TimeUnit.MILLISECONDS);
    }
}

