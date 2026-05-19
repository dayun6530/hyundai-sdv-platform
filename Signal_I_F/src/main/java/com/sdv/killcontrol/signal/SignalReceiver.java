package com.sdv.killcontrol.signal;

import com.sdv.killcontrol.event.KillSignalEvent;
import com.sdv.killcontrol.runtime.CarProcessRuntime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.event.EventListener;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Signal Receiver — 웹 마이그레이션 시 <strong>수정된 부분만</strong> 이 클래스에 반영됨.
 * <p>
 * <strong>제거:</strong> CMD 표준입력을 블로킹하며 {@code Scanner} 등으로 {@code KILL} 문자열을
 * 기다리던 루프 전체(및 Ctrl+S에 의존하던 입력 처리)는 삭제됨.
 * <p>
 * <strong>추가:</strong> Spring {@code KillSignalEvent}를 {@code @EventListener}로 비동기 수신.
 * <p>
 * <strong>유지:</strong> 차량별 {@code ExternalSignals_<car>.cmd}에 KILL 라인을 기록하면
 * 플랫폼 SignalReceiver/MqttVehicleAgentHost가 기존 graceful shutdown 경로로 처리한다.
 * <p>
 * 데이터 흐름 (전체 5단계 중 3→5):
 * <ol start="3">
 *   <li>Controller가 {@link KillSignalEvent} 발행 완료</li>
 *   <li>Spring이 본 리스너를 {@code @Async}로 호출 (비동기 수신)</li>
 *   <li>SignalReceiver: {@code car_1}..{@code car_10}에 맞는 {@code config/signals/ExternalSignals_<car>.cmd}에 기록</li>
 *   <li>기존 플랫폼(FeatureEvaluator 등)이 해당 파일을 읽고 비우며, 기존 graceful shutdown 경로를 그대로 실행</li>
 * </ol>
 */
@Component
public class SignalReceiver {

    private static final Logger log = LoggerFactory.getLogger(SignalReceiver.class);

    private final Path externalSignalsDir;
    private final CarProcessRuntime runtime;

    public SignalReceiver(CarProcessRuntime runtime) {
        this.runtime = runtime;
        final Path repoRoot = resolveRepoRoot();
        final Path configBase = repoRoot.resolve("sdv-feature-flag-platform").resolve("config");
        this.externalSignalsDir = configBase.resolve("signals");
    }

    @Async
    @EventListener
    public void onKillSignal(KillSignalEvent event) {
        log.info(
                "KillSignalEvent received (source={}, requestedAt={}, car={}, command={})",
                event.getSource(),
                event.getRequestedAt(),
                event.getCar(),
                event.getCommand()
        );

        final Path target = resolveTargetFile(event.getCar());
        appendExternalSignal(target, event.getCommand());

        try {
            if (event.getCommand() != null && event.getCommand().toUpperCase().startsWith("KILL:")) {
                runtime.shutdownAllGracefully();
            }
        } catch (Exception e) {
            log.warn("Failed to trigger immediate shutdown (ignored): {}", e.toString());
        }
    }

    private Path resolveTargetFile(String car) {
        return externalSignalsDir.resolve("ExternalSignals_" + car + ".cmd");
    }

    private static void appendExternalSignal(Path file, String command) {
        try {
            Files.createDirectories(file.getParent());
            Files.writeString(
                    file,
                    command + System.lineSeparator(),
                    StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.APPEND
            );
            log.info("Wrote external signal: {} -> {}", command, file.toAbsolutePath());
        } catch (IOException e) {
            throw new IllegalStateException("Failed to write external signal file: " + file.toAbsolutePath(), e);
        }
    }

    private static Path resolveRepoRoot() {
        Path p = Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize();
        for (int i = 0; i < 6 && p != null; i++) {
            if (Files.isDirectory(p.resolve("kill-control-service"))
                    && Files.isDirectory(p.resolve("sdv-feature-flag-platform"))) {
                return p;
            }
            p = p.getParent();
        }
        return Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize();
    }
}
