package com.sdv.killcontrol.web;

import com.sdv.killcontrol.event.KillSignalEvent;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.CrossOrigin;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.time.Instant;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * REST API entry for web UI kill requests.
 * <p>
 * 데이터 흐름 (1 → 2 → 3단계):
 * <ol>
 *   <li>Browser: {@code index.html}에서 확인 후 {@code fetch} POST {@code /api/system/kill}</li>
 *   <li>본 컨트롤러: 프로세스를 직접 종료하지 않고 {@link KillSignalEvent}만 {@code ApplicationEventPublisher}로 발행</li>
 *   <li>Spring이 {@link com.sdv.killcontrol.signal.SignalReceiver}의 {@code @EventListener}(비동기)로 전달 → 기존 {@code shutdownAllGracefully()} 호출</li>
 * </ol>
 */
@RestController
@RequestMapping("/api/system")
@CrossOrigin(origins = "*")
public class SystemKillController {

    private final ApplicationEventPublisher eventPublisher;
    private static final Pattern CAR_ID_PATTERN = Pattern.compile("^car_(\\d{1,2})$", Pattern.CASE_INSENSITIVE);

    public SystemKillController(ApplicationEventPublisher eventPublisher) {
        this.eventPublisher = eventPublisher;
    }

    @PostMapping("/kill")
    public ResponseEntity<Map<String, Object>> requestKill(@RequestBody KillRequest request) {
        final String car = normalizeCar(request.car());
        final String command = normalizeCommand(request.command());

        eventPublisher.publishEvent(new KillSignalEvent(this, Instant.now(), "web-api", car, command));
        return ResponseEntity
                .status(HttpStatus.ACCEPTED)
                .body(Map.of(
                        "accepted", true,
                        "car", car,
                        "command", command,
                        "message", "Kill command accepted and queued."
                ));
    }

    /**
     * Web payload:
     * <ul>
     *   <li>car: {@code car_1} .. {@code car_10}</li>
     *   <li>command: e.g. {@code KILL:AdasFeature} / {@code KILL:NavigationFeature:REASON}</li>
     * </ul>
     */
    public record KillRequest(String car, String command) {}

    private static String normalizeCar(String car) {
        final String raw = (car == null) ? "" : car.trim();
        final Matcher m = CAR_ID_PATTERN.matcher(raw);
        if (!m.matches()) {
            throw new IllegalArgumentException("car must be 'car_1'..'car_10'");
        }
        final int n = Integer.parseInt(m.group(1));
        if (n < 1 || n > 10) {
            throw new IllegalArgumentException("car must be 'car_1'..'car_10'");
        }
        return "car_" + n;
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
