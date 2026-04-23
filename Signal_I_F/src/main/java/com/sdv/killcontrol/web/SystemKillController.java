package com.sdv.killcontrol.web;

import com.sdv.killcontrol.kill.KillCommandWriter;
import com.sdv.killcontrol.kill.KillFeedbackService;
import com.sdv.killcontrol.kill.KillRequestStore;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.CrossOrigin;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.nio.file.Files;
import java.time.Instant;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * REST API entry for web UI kill requests.
 * <p>
 * Writes {@code ExternalSignals_*.cmd} synchronously, then polls platform {@code SignalsOutbound.cmd}
 * for {@code WARNING:…:KillSwitch} echoes so the UI can show Success / Failed.
 */
@RestController
@RequestMapping("/api/system")
@CrossOrigin(origins = "*")
public class SystemKillController {

    private final KillCommandWriter killCommandWriter;
    private final KillFeedbackService killFeedbackService;
    private final KillRequestStore killRequestStore;

    public SystemKillController(KillCommandWriter killCommandWriter,
                                KillFeedbackService killFeedbackService,
                                KillRequestStore killRequestStore) {
        this.killCommandWriter = killCommandWriter;
        this.killFeedbackService = killFeedbackService;
        this.killRequestStore = killRequestStore;
    }

    @PostMapping("/kill")
    public ResponseEntity<Map<String, Object>> requestKill(@RequestBody KillRequest request) {
        final String carId = normalizeCarId(request.car());
        final String command = normalizeCommand(request.command());

        var outbound = killFeedbackService.outboundSignalsPath(carId);
        long outboundStart = 0L;
        try {
            outboundStart = Files.isRegularFile(outbound) ? Files.size(outbound) : 0L;
        } catch (Exception ignored) {
            outboundStart = 0L;
        }

        var evalLog = killFeedbackService.evaluatorLogPath(carId);
        long evalLogStart = 0L;
        try {
            evalLogStart = Files.isRegularFile(evalLog) ? Files.size(evalLog) : 0L;
        } catch (Exception ignored) {
            evalLogStart = 0L;
        }

        Map<String, Long> targetLogStartPositions =
                killFeedbackService.snapshotTargetLogPositions(carId, command);

        killCommandWriter.appendKill(carId, command);
        KillRequestStore.KillRequestContext ctx =
                killRequestStore.create(
                        carId,
                        command,
                        outboundStart,
                        evalLogStart,
                        targetLogStartPositions
                );

        Map<String, Object> body = new LinkedHashMap<>();
        body.put("accepted", true);
        body.put("car", carId);
        body.put("command", command);
        body.put("requestedAt", Instant.now().toString());
        body.put("requestId", ctx.id());
        body.put("message", "성공적으로 전송했습니다.");
        body.put("killOutcome", "");
        return ResponseEntity.status(HttpStatus.ACCEPTED).body(body);
    }

    @GetMapping("/kill/status")
    public ResponseEntity<Map<String, Object>> getKillStatus(@RequestParam("requestId") String requestId) {
        var ctxOpt = killRequestStore.get(requestId);
        if (ctxOpt.isEmpty()) {
            return ResponseEntity.status(HttpStatus.NOT_FOUND)
                    .body(Map.of("error", "unknown requestId"));
        }
        var ctx = ctxOpt.get();
        KillFeedbackService.KillAckResult r = killFeedbackService.checkKillAck(
                ctx.car(),
                ctx.command(),
                ctx.outboundStartPosition(),
                ctx.evaluatorLogStartPosition(),
                ctx.targetLogStartPositions(),
                ctx.startedAtEpochMs()
        );

        Map<String, Object> body = new HashMap<>();
        body.put("requestId", ctx.id());
        body.put("car", ctx.car());
        body.put("command", ctx.command());
        body.put("state", r.state().name());
        body.put("killOutcome", r.killOutcome());
        body.put("message", r.message());

        boolean done = r.state() == KillFeedbackService.KillAckState.Success
                || r.state() == KillFeedbackService.KillAckState.failed;
        body.put("done", done);
        if (done) {
            // one-shot result: allow cleanup
            killRequestStore.remove(ctx.id());
        }
        return ResponseEntity.ok(body);
    }

    /**
     * Web payload:
     * <ul>
     *   <li>car: "A" | "B"</li>
     *   <li>command: e.g. "KILL:AdasFeature" / "KILL:NavigationFeature:REASON"</li>
     * </ul>
     */
    public record KillRequest(String car, String command) {}

    private static String normalizeCarId(String car) {
        final String c = (car == null) ? "" : car.trim();
        if (c.isEmpty()) {
            throw new IllegalArgumentException("car is required (e.g., car_1)");
        }
        // Minimal allowlist: car_<n>, plus legacy car_A/car_B.
        if (c.matches("^car_[0-9]+$")) {
            return c;
        }
        throw new IllegalArgumentException("car must be like 'car_1'");
    }

    private static String normalizeCommand(String command) {
        final String cmd = (command == null) ? "" : command.trim();
        if (cmd.isEmpty()) {
            throw new IllegalArgumentException("command is required");
        }
        if (!cmd.toUpperCase().startsWith("KILL:")) {
            throw new IllegalArgumentException("command must start with 'KILL:'");
        }
        return cmd;
    }
}
