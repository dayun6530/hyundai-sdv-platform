package com.sdv.killcontrol.sdv.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sdv.killcontrol.sdv.messaging.TelemetryMessageHandler;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.integration.annotation.ServiceActivator;
import org.springframework.integration.channel.DirectChannel;
import org.springframework.integration.mqtt.core.DefaultMqttPahoClientFactory;
import org.springframework.integration.mqtt.core.MqttPahoClientFactory;
import org.springframework.integration.mqtt.inbound.MqttPahoMessageDrivenChannelAdapter;
import org.springframework.integration.mqtt.outbound.MqttPahoMessageHandler;
import org.springframework.messaging.MessageChannel;
import org.springframework.messaging.MessageHandler;

import java.nio.charset.StandardCharsets;
import java.util.Objects;

@Configuration
@EnableConfigurationProperties(SdvMqttProperties.class)
public class MqttIntegrationConfig {
    private static final Logger log = LoggerFactory.getLogger(MqttIntegrationConfig.class);

    public static final String MQTT_OUTBOUND_CHANNEL = "sdvMqttOutboundChannel";
    public static final String MQTT_INBOUND_CHANNEL = "sdvMqttInboundChannel";

    @Bean
    public MqttPahoClientFactory mqttClientFactory(SdvMqttProperties props) {
        DefaultMqttPahoClientFactory factory = new DefaultMqttPahoClientFactory();

        MqttConnectOptions options = new MqttConnectOptions();
        options.setServerURIs(new String[]{Objects.requireNonNull(props.brokerUri(), "sdv.mqtt.broker-uri")});
        options.setAutomaticReconnect(true);
        options.setCleanSession(false);

        if (props.username() != null && !props.username().isBlank()) {
            options.setUserName(props.username());
        }
        if (props.password() != null && !props.password().isBlank()) {
            options.setPassword(props.password().toCharArray());
        }

        factory.setConnectionOptions(options);
        return factory;
    }

    @Bean(name = MQTT_OUTBOUND_CHANNEL)
    public MessageChannel mqttOutboundChannel() {
        return new DirectChannel();
    }

    @Bean
    @ServiceActivator(inputChannel = MQTT_OUTBOUND_CHANNEL)
    public MessageHandler mqttOutbound(SdvMqttProperties props, MqttPahoClientFactory factory) {
        String clientId = Objects.requireNonNull(props.clientId(), "sdv.mqtt.client-id") + "-out";
        MqttPahoMessageHandler handler = new MqttPahoMessageHandler(clientId, factory);
        handler.setAsync(true);
        handler.setDefaultQos(requireQosAtLeastOne(props.qos()));
        handler.setDefaultRetained(false);
        return handler;
    }

    @Bean(name = MQTT_INBOUND_CHANNEL)
    public MessageChannel mqttInboundChannel() {
        return new DirectChannel();
    }

    @Bean
    public MqttPahoMessageDrivenChannelAdapter mqttInbound(SdvMqttProperties props, MqttPahoClientFactory factory) {
        String clientId = Objects.requireNonNull(props.clientId(), "sdv.mqtt.client-id") + "-in";
        String topic = Objects.requireNonNull(props.telemetryTopic(), "sdv.mqtt.telemetry-topic");

        log.info("Subscribing telemetry: topic={} clientId={}", topic, clientId);

        MqttPahoMessageDrivenChannelAdapter adapter =
                new MqttPahoMessageDrivenChannelAdapter(clientId, factory, topic);
        adapter.setOutputChannel(mqttInboundChannel());
        adapter.setQos(requireQosAtLeastOne(props.qos()));
        return adapter;
    }

    @Bean
    @ServiceActivator(inputChannel = MQTT_INBOUND_CHANNEL)
    public MessageHandler telemetryInboundHandler(ObjectMapper objectMapper, TelemetryMessageHandler handler) {
        return message -> {
            Object payloadObj = message.getPayload();
            byte[] payloadBytes;
            if (payloadObj instanceof byte[] b) {
                payloadBytes = b;
            } else if (payloadObj instanceof String s) {
                payloadBytes = s.getBytes(StandardCharsets.UTF_8);
            } else {
                payloadBytes = String.valueOf(payloadObj).getBytes(StandardCharsets.UTF_8);
            }
            handler.handleTelemetry(message.getHeaders(), payloadBytes, objectMapper);
        };
    }

    private static int requireQosAtLeastOne(Integer qos) {
        int q = (qos == null) ? 1 : qos;
        if (q < 1) throw new IllegalArgumentException("sdv.mqtt.qos must be >= 1");
        return q;
    }
}

