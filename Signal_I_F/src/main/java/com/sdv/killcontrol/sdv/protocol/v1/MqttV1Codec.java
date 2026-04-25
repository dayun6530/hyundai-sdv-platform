package com.sdv.killcontrol.sdv.protocol.v1;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.time.Instant;
import java.time.format.DateTimeFormatter;
import java.util.Objects;
import java.util.UUID;

public final class MqttV1Codec {
    public static final String CMD_DEPLOY_FEATURE = "DEPLOY_FEATURE";
    public static final String CMD_KILL_SWITCH = "KILL_SWITCH";

    private MqttV1Codec() {}

    public static String encode(ObjectMapper mapper, MqttMessageV1 message) {
        try {
            return mapper.writeValueAsString(message);
        } catch (JsonProcessingException e) {
            throw new IllegalArgumentException("encode failed: " + e.getMessage(), e);
        }
    }

    public static DecodeResult decode(ObjectMapper mapper, String json) {
        try {
            MqttMessageV1 msg = mapper.readValue(json, MqttMessageV1.class);
            return validate(msg);
        } catch (Exception e) {
            return DecodeResult.error(ErrorCode.INVALID_JSON, "invalid json: " + e.getMessage());
        }
    }

    public static MqttMessageV1 newDeployMessage(String vehicleId,
                                                 String featureName,
                                                 String version,
                                                 String action,
                                                 String binaryPath) {
        String msgId = UUID.randomUUID().toString();
        return new MqttMessageV1(
                MqttMessageV1.PROTOCOL_VERSION,
                new MessageHeader(msgId, DateTimeFormatter.ISO_INSTANT.format(Instant.now()), vehicleId),
                CMD_DEPLOY_FEATURE,
                new FeaturePayload(featureName, version, action, binaryPath, null),
                null
        );
    }

    public static MqttMessageV1 newKillSwitchMessage(String vehicleId, String featureName) {
        String msgId = UUID.randomUUID().toString();
        return new MqttMessageV1(
                MqttMessageV1.PROTOCOL_VERSION,
                new MessageHeader(msgId, DateTimeFormatter.ISO_INSTANT.format(Instant.now()), vehicleId),
                CMD_KILL_SWITCH,
                new FeaturePayload(featureName, "", "KILL", "", null),
                null
        );
    }

    public static MqttMessageV1 newShadowRunMessage(String vehicleId,
                                                    String currentFeature,
                                                    String shadowFeature,
                                                    String shadowVersion,
                                                    String binaryPath) {
        String msgId = UUID.randomUUID().toString();
        return new MqttMessageV1(
                MqttMessageV1.PROTOCOL_VERSION,
                new MessageHeader(msgId, DateTimeFormatter.ISO_INSTANT.format(Instant.now()), vehicleId),
                CMD_DEPLOY_FEATURE,
                new FeaturePayload(shadowFeature, shadowVersion, "SHADOW_RUN", binaryPath, currentFeature),
                null
        );
    }

    private static DecodeResult validate(MqttMessageV1 msg) {
        if (msg == null) return DecodeResult.error(ErrorCode.INVALID_FIELD, "null message");
        if (!Objects.equals(msg.protocolVersion(), MqttMessageV1.PROTOCOL_VERSION)) {
            return DecodeResult.error(ErrorCode.UNSUPPORTED_VERSION, "protocol_version=" + msg.protocolVersion());
        }
        if (msg.header() == null) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing header");
        if (isBlank(msg.header().msgId())) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing header.msg_id");
        if (isBlank(msg.header().vehicleId())) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing header.vehicle_id");
        if (isBlank(msg.command())) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing command");
        if (msg.payload() == null) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing payload");
        if (isBlank(msg.payload().featureName())) return DecodeResult.error(ErrorCode.MISSING_FIELD, "missing payload.feature_name");
        return DecodeResult.ok(msg);
    }

    private static boolean isBlank(String s) {
        return s == null || s.trim().isEmpty();
    }

    public record DecodeResult(MqttMessageV1 message, ErrorInfo error) {
        public static DecodeResult ok(MqttMessageV1 msg) {
            return new DecodeResult(msg, null);
        }

        public static DecodeResult error(ErrorCode code, String message) {
            return new DecodeResult(null, ErrorInfo.of(code, message));
        }

        public boolean isOk() {
            return message != null;
        }
    }
}

