package com.sdv.killcontrol.sdv.web;

import com.sdv.killcontrol.sdv.service.KillSwitchService;
import com.sdv.killcontrol.sdv.service.SdvControlTower;
import com.sdv.killcontrol.sdv.service.ShadowModeService;
import com.sdv.killcontrol.sdv.store.CommandStatus;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;

@RestController
@RequestMapping("/api/v1")
@CrossOrigin(origins = "*")
public class SdvControlApiController {
    private final KillSwitchService killSwitchService;
    private final ShadowModeService shadowModeService;
    private final SdvControlTower controlTower;

    public SdvControlApiController(KillSwitchService killSwitchService,
                                   ShadowModeService shadowModeService,
                                   SdvControlTower controlTower) {
        this.killSwitchService = killSwitchService;
        this.shadowModeService = shadowModeService;
        this.controlTower = controlTower;
    }

    @PostMapping("/control/kill-switch")
    public CompletableFuture<ResponseEntity<Map<String, Object>>> killSwitch(@RequestBody KillSwitchRequest req) {
        Duration timeout = (req.timeoutMs() == null) ? Duration.ofSeconds(5) : Duration.ofMillis(req.timeoutMs());
        return killSwitchService.kill(req.vehicleId(), req.targetFeature(), timeout)
                .handle((status, err) -> toResponse("KillSwitch", status, err));
    }

    @PostMapping("/deploy")
    public CompletableFuture<ResponseEntity<Map<String, Object>>> deploy(@RequestBody DeployRequest req) {
        Duration timeout = (req.timeoutMs() == null) ? Duration.ofSeconds(10) : Duration.ofMillis(req.timeoutMs());
        return controlTower.sendDeploy(req.vehicleId(), req.targetFeature(), req.version(), req.payloadUrl(), timeout)
                .handle((status, err) -> toResponse("Deploy", status, err));
    }

    @PostMapping("/control/shadow-mode")
    public CompletableFuture<ResponseEntity<Map<String, Object>>> shadowMode(@RequestBody ShadowModeRequest req) {
        Duration timeout = (req.timeoutMs() == null) ? Duration.ofSeconds(5) : Duration.ofMillis(req.timeoutMs());
        return shadowModeService.setShadowMode(req.vehicleId(), req.runningFeature(), req.shadowFeature(), req.shadowVersion(), timeout)
                .handle((status, err) -> toResponse("ShadowMode", status, err));
    }

    public record KillSwitchRequest(String vehicleId, String targetFeature, Long timeoutMs) {}

    public record DeployRequest(String vehicleId, String targetFeature, String version, String payloadUrl, Long timeoutMs) {}

    public record ShadowModeRequest(String vehicleId, String runningFeature, String shadowFeature, String shadowVersion, Long timeoutMs) {}

    private static ResponseEntity<Map<String, Object>> toResponse(String cmd, CommandStatus status, Throwable err) {
        if (err != null) {
            final String msg = (err.getMessage() != null) ? err.getMessage() : err.toString();
            return ResponseEntity.status(HttpStatus.GATEWAY_TIMEOUT).body(Map.of(
                    "accepted", true,
                    "command", cmd,
                    "completed", false,
                    "error", msg
            ));
        }
        return ResponseEntity.status(HttpStatus.OK).body(Map.of(
                "accepted", true,
                "command", cmd,
                "completed", true,
                "vehicleId", status.vehicleId(),
                "requestId", status.requestId(),
                "status", status.status(),
                "detail", status.detail(),
                "updatedAt", status.lastUpdated().toString()
        ));
    }
}

