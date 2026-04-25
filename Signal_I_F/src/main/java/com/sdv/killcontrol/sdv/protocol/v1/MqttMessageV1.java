package com.sdv.killcontrol.sdv.protocol.v1;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;

@JsonInclude(JsonInclude.Include.NON_NULL)
public record MqttMessageV1(
        @JsonProperty("protocol_version")
        @JsonAlias({"protocolVersion"})
        String protocolVersion,

        MessageHeader header,

        String command,

        FeaturePayload payload,

        ErrorInfo error
) {
    public static final String PROTOCOL_VERSION = "1.0";
}

