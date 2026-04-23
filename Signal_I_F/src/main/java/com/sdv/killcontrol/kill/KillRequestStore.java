package com.sdv.killcontrol.kill;

import org.springframework.stereotype.Component;

import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class KillRequestStore {

    public record KillRequestContext(
            String id,
            String car,
            String command,
            long outboundStartPosition,
            long evaluatorLogStartPosition,
            Map<String, Long> targetLogStartPositions,
            long startedAtEpochMs
    ) {}

    private final ConcurrentHashMap<String, KillRequestContext> byId = new ConcurrentHashMap<>();

    public KillRequestContext create(String car,
                                     String command,
                                     long outboundStartPosition,
                                     long evaluatorLogStartPosition,
                                     Map<String, Long> targetLogStartPositions) {
        String id = UUID.randomUUID().toString();
        KillRequestContext ctx = new KillRequestContext(
                id,
                car,
                command,
                outboundStartPosition,
                evaluatorLogStartPosition,
                Map.copyOf(targetLogStartPositions),
                System.currentTimeMillis()
        );
        byId.put(id, ctx);
        return ctx;
    }

    public Optional<KillRequestContext> get(String id) {
        if (id == null || id.isBlank()) {
            return Optional.empty();
        }
        return Optional.ofNullable(byId.get(id));
    }

    public void remove(String id) {
        if (id != null) {
            byId.remove(id);
        }
    }
}

