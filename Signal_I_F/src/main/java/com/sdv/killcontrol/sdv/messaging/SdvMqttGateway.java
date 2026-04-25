package com.sdv.killcontrol.sdv.messaging;

import org.springframework.integration.annotation.MessagingGateway;
import org.springframework.messaging.handler.annotation.Header;

import static org.springframework.integration.mqtt.support.MqttHeaders.TOPIC;
import static com.sdv.killcontrol.sdv.config.MqttIntegrationConfig.MQTT_OUTBOUND_CHANNEL;

@MessagingGateway(defaultRequestChannel = MQTT_OUTBOUND_CHANNEL)
public interface SdvMqttGateway {
    void publish(@Header(TOPIC) String topic, String payload);
}

