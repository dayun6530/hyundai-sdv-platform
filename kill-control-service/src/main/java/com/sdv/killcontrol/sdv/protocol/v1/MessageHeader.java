package com.sdv.killcontrol.sdv.protocol.v1;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;

public record MessageHeader(
        @JsonProperty("msg_id")
        @JsonAlias({"msgId"})
        String msgId,

        String timestamp,

        @JsonProperty("vehicle_id")
        @JsonAlias({"vehicleId"})
        String vehicleId
) {
}

