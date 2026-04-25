package com.sdv.killcontrol.sdv.config;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties(prefix = "sdv.mqtt")
public record SdvMqttProperties(
        String brokerUri,
        String clientId,
        String username,
        String password,
        Integer qos,
        String telemetryTopic
) {
}

