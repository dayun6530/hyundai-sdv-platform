package com.sdv.killcontrol.event;

import org.springframework.context.ApplicationEvent;

import java.time.Instant;

/**
 * Domain event: operator requested a platform kill (formerly typed as KILL in CMD + Ctrl+S).
 * <p>
 * Data flow (step 2 → 3):
 * <ul>
 *   <li>{@code SystemKillController} receives HTTP POST {@code /api/system/kill}</li>
 *   <li>Controller publishes this event via {@code ApplicationEventPublisher} (does not kill processes itself)</li>
 *   <li>{@link com.sdv.killcontrol.signal.SignalReceiver} receives the event asynchronously and calls existing shutdown</li>
 * </ul>
 */
public class KillSignalEvent extends ApplicationEvent {

    private final Instant requestedAt;
    private final String source;
    private final String car;       // "A" or "B"
    private final String command;   // e.g. "KILL:AdasFeature" or "KILL:NavigationFeature:REASON"

    public KillSignalEvent(Object publisherSource,
                          Instant requestedAt,
                          String source,
                          String car,
                          String command) {
        super(publisherSource);
        this.requestedAt = requestedAt;
        this.source = source;
        this.car = car;
        this.command = command;
    }

    public Instant getRequestedAt() {
        return requestedAt;
    }

    public String getSource() {
        return source;
    }

    public String getCar() {
        return car;
    }

    public String getCommand() {
        return command;
    }
}
