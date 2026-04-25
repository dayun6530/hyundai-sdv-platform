package com.sdv.killcontrol.kill;

import org.springframework.stereotype.Service;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

/**
 * Resolves which logical feature names the C++ {@code FeatureEvaluator} will emit
 * {@code WARNING:<name>:KillSwitch…} lines for, then polls {@code SignalsOutbound.cmd} for them.
 * <p>
 * Logic mirrors {@code sdv-feature-flag-platform/.../FeatureEvaluator.cpp} kill-target resolution
 * using the same {@code config/car/<carId>.conf} as the platform instance.
 */
@Service
public class KillFeedbackService {

    private static final String KILL_PREFIX = "KILL:";
    private static final String KILL_ALL_SUFFIX = "_ALL";
    private static final String[] DOMAINS = {"NavigationFeature", "AdasFeature", "EvEnergyFeature"};
    private static final int POLL_MS = 350;
    private static final int TIMEOUT_MS = 12_000;

    private final Path platformRoot;

    public KillFeedbackService() {
        this.platformRoot = Path.of(System.getProperty("user.dir")).resolve("sdv-feature-flag-platform");
    }

    public Path outboundSignalsPath(String car) {
        return platformRoot.resolve("logs").resolve(car).resolve("SignalsOutbound.cmd");
    }

    public Path evaluatorLogPath(String car) {
        return platformRoot.resolve("logs").resolve(car).resolve("FeatureEvaluator.log");
    }

    public Path featureLogPath(String carId, String featureName) {
        return platformRoot.resolve("logs").resolve(carId).resolve(featureName + ".log");
    }

    public int timeoutMs() {
        return TIMEOUT_MS;
    }

    /**
     * 한국어 사용자 메시지. {@code killOutcome}은 {@link #waitForKillAck}의 반환값({@code Success} / {@code failed})과 동일해야 한다.
     */
    public String buildUserMessage(String car, String command, String killOutcome) {
        List<String> targets = expectedWarningTargets(car, command);
        if ("Success".equals(killOutcome)) {
            if (targets.isEmpty()) {
                return "요청한 KILL이 성공적으로 처리되었습니다!";
            }
            if (targets.size() == 1) {
                return targets.get(0) + "가 성공적으로 KILL되었습니다!";
            }
            return String.join(", ", targets) + "가 성공적으로 KILL되었습니다!";
        }
        if (targets.size() == 1) {
            return targets.get(0)
                    + "에 대한 KILL을 플랫폼에서 확인하지 못했습니다. SDV(EventBus, SignalReceiver, FeatureEvaluator)가 실행 중인지 확인해 주세요.";
        }
        if (targets.size() > 1) {
            return String.join(", ", targets)
                    + "에 대한 KILL을 플랫폼에서 확인하지 못했습니다. SDV 플랫폼이 실행 중인지 확인해 주세요.";
        }
        return "KILL 명령은 기록되었으나, 플랫폼에서 처리 결과를 확인하지 못했습니다. SDV가 실행 중인지 확인해 주세요.";
    }

    public enum KillAckState { pending, Success, failed }

    public record KillAckResult(KillAckState state, String message, String killOutcome) {}

    /**
     * Non-blocking ack check used by polling API.
     * - pending: no confirmation yet and within timeout
     * - Success: all expected WARNING lines observed
     * - failed: timeout elapsed or invalid shorthand mapping
     */
    public KillAckResult checkKillAck(String car,
                                      String command,
                                      long outboundStartPosition,
                                      long evaluatorLogStartPosition,
                                      Map<String, Long> targetLogStartPositions,
                                      long startedAtEpochMs) {
        List<String> expected = expectedWarningTargets(car, command);
        if (expected.isEmpty()) {
            String msg = buildUserMessage(car, command, "failed");
            return new KillAckResult(KillAckState.failed, msg, "failed");
        }

        // Success means the actual target process logs a new "stopped" line after this request.
        // ACK logs from FeatureEvaluator are not enough because typos can still produce KILL events
        // for nonexistent logical names.
        if (allTargetsActuallyStopped(car, expected, targetLogStartPositions)) {
            String msg = buildUserMessage(car, command, "Success");
            return new KillAckResult(KillAckState.Success, msg, "Success");
        }

        long elapsed = System.currentTimeMillis() - startedAtEpochMs;
        if (elapsed >= TIMEOUT_MS) {
            String msg = buildUserMessage(car, command, "failed");
            return new KillAckResult(KillAckState.failed, msg, "failed");
        }

        return new KillAckResult(KillAckState.pending, "KILL 확인 중…", "");
    }

    private boolean allTargetsActuallyStopped(String carId,
                                              List<String> targets,
                                              Map<String, Long> targetLogStartPositions) {
        for (String t : targets) {
            Path logPath = featureLogPath(carId, t);
            if (!Files.isRegularFile(logPath)) {
                return false;
            }
            final long start = targetLogStartPositions.getOrDefault(t, 0L);
            try {
                long len = Files.size(logPath);
                long safeStart = Math.min(start, len);
                if (len <= safeStart) {
                    return false;
                }
                String chunk = readUtf8Range(logPath, safeStart, len);
                if (!chunk.contains("stopped")) {
                    return false;
                }
            } catch (IOException e) {
                return false;
            }
        }
        return true;
    }

    private static String readUtf8Range(Path file, long startInclusive, long endExclusive)
            throws IOException {
        if (endExclusive <= startInclusive) {
            return "";
        }
        int n = (int) Math.min(Integer.MAX_VALUE, endExclusive - startInclusive);
        byte[] buf = new byte[n];
        try (RandomAccessFile raf = new RandomAccessFile(file.toFile(), "r")) {
            raf.seek(startInclusive);
            raf.readFully(buf);
            return new String(buf, StandardCharsets.UTF_8);
        }
    }

    private List<String> expectedWarningTargets(String carId, String command) {
        String cmd = command == null ? "" : command.trim();
        if (!cmd.toUpperCase(Locale.ROOT).startsWith(KILL_PREFIX)) {
            return List.of();
        }
        String rest = cmd.substring(KILL_PREFIX.length());
        int second = rest.indexOf(':');
        String rawTarget = (second >= 0 ? rest.substring(0, second) : rest).trim();

        // carId is the instance id (e.g., car_1). Group configs live under config/car/.
        CarConfig cfg = readCarConfig(platformRoot.resolve("config").resolve("car").resolve(carId + ".conf"));
        Set<String> deployed = cfg.featureExecNames();

        List<String> killTargets = resolveCarLineKillTargetsUsingConfig(rawTarget, deployed);
        if (killTargets.isEmpty()) {
            killTargets = expandAmbiguousCarLineKillTargets(rawTarget);
        }
        if (killTargets.isEmpty()) {
            killTargets = List.of(normalizeCarLineTarget(rawTarget, cfg.abVariant()));
        }

        return flattenKillAll(killTargets);
    }

    public Map<String, Long> snapshotTargetLogPositions(String carId, String command) {
        List<String> targets = expectedWarningTargets(carId, command);
        Map<String, Long> positions = new HashMap<>();
        for (String target : targets) {
            Path logPath = featureLogPath(carId, target);
            long size = 0L;
            try {
                size = Files.isRegularFile(logPath) ? Files.size(logPath) : 0L;
            } catch (IOException ignored) {
                size = 0L;
            }
            positions.put(target, size);
        }
        return positions;
    }

    private static List<String> flattenKillAll(List<String> killTargets) {
        List<String> out = new ArrayList<>();
        for (String killName : killTargets) {
            if (killName.length() > KILL_ALL_SUFFIX.length()
                    && killName.endsWith(KILL_ALL_SUFFIX)) {
                String base = killName.substring(0, killName.length() - KILL_ALL_SUFFIX.length());
                String shadowName = shadowLogicalNameForPrimaryBase(base);
                out.add(shadowName);
                out.add(base);
            } else {
                out.add(killName);
            }
        }
        return dedupePreserveOrder(out);
    }

    private static List<String> dedupePreserveOrder(List<String> in) {
        LinkedHashSet<String> set = new LinkedHashSet<>(in);
        return new ArrayList<>(set);
    }

    private static String shadowLogicalNameForPrimaryBase(String base) {
        if (base.length() >= 2) {
            boolean endsA = base.endsWith("_A");
            boolean endsB = base.endsWith("_B");
            if (endsA || endsB) {
                return base + "_Shadow";
            }
        }
        return base + "Shadow";
    }

    private static boolean containsCarLineSuffix(String name) {
        return name.contains("_A") || name.contains("_B");
    }

    private static boolean endsWith(String s, String suffix) {
        return s.length() >= suffix.length() && s.endsWith(suffix);
    }

    private static String normalizeCarLineTarget(String raw, Optional<Character> variantOpt) {
        if (variantOpt.isEmpty()) {
            return raw;
        }
        char v = variantOpt.get();
        if (v != 'A' && v != 'B') {
            return raw;
        }
        if (raw.isEmpty() || containsCarLineSuffix(raw)) {
            return raw;
        }
        final String carLineShadowSuffix = "_Shadow";
        if (endsWith(raw, carLineShadowSuffix)) {
            String base = raw.substring(0, raw.length() - carLineShadowSuffix.length());
            if (base.isEmpty() || containsCarLineSuffix(base)) {
                return raw;
            }
            return base + "_" + v + carLineShadowSuffix;
        }
        if (endsWith(raw, "Shadow")) {
            String base = raw.substring(0, raw.length() - "Shadow".length());
            if (base.isEmpty()) {
                return raw;
            }
            return base + "_" + v + "_Shadow";
        }
        return raw + "_" + v;
    }

    private static List<String> resolveCarLineKillTargetsUsingConfig(String raw, Set<String> deployed) {
        List<String> matches = new ArrayList<>();
        for (String dom : DOMAINS) {
            if (raw.equals(dom)) {
                String a = dom + "_A";
                String b = dom + "_B";
                if (deployed.contains(a)) {
                    matches.add(a);
                }
                if (deployed.contains(b)) {
                    matches.add(b);
                }
                return matches;
            }
            if (raw.equals(dom + "_Shadow")) {
                String as = dom + "_A_Shadow";
                String bs = dom + "_B_Shadow";
                if (deployed.contains(as)) {
                    matches.add(as);
                }
                if (deployed.contains(bs)) {
                    matches.add(bs);
                }
                String leg = dom + "Shadow";
                if (deployed.contains(leg)) {
                    matches.add(leg);
                }
                return matches;
            }
            if (raw.equals(dom + "Shadow")) {
                String as = dom + "_A_Shadow";
                String bs = dom + "_B_Shadow";
                if (deployed.contains(as)) {
                    matches.add(as);
                }
                if (deployed.contains(bs)) {
                    matches.add(bs);
                }
                if (deployed.contains(dom + "Shadow")) {
                    matches.add(dom + "Shadow");
                }
                return matches;
            }
        }
        return matches;
    }

    private static List<String> expandAmbiguousCarLineKillTargets(String raw) {
        for (String dom : DOMAINS) {
            if (raw.equals(dom)) {
                return List.of(dom + "_A", dom + "_B");
            }
            if (raw.equals(dom + "_Shadow")) {
                return List.of(dom + "_A_Shadow", dom + "_B_Shadow");
            }
            if (raw.equals(dom + "Shadow")) {
                return List.of(dom + "_A_Shadow", dom + "_B_Shadow");
            }
        }
        return List.of();
    }

    private static CarConfig readCarConfig(Path path) {
        Set<String> features = new LinkedHashSet<>();
        Optional<Character> ab = Optional.empty();
        if (!Files.isRegularFile(path)) {
            return new CarConfig(features, ab);
        }
        try {
            List<String> lines = Files.readAllLines(path, StandardCharsets.UTF_8);
            for (String line : lines) {
                String t = trim(line);
                if (t.isEmpty() || t.startsWith("#")) {
                    continue;
                }
                String[] parts = t.split("\\s+");
                if (parts.length < 2) {
                    continue;
                }
                String key = parts[0];
                String value = parts[1];
                if ("feature".equals(key)) {
                    features.add(value);
                } else if ("ab_mode".equals(key)) {
                    if (value.length() == 1) {
                        char c = value.charAt(0);
                        if (c == 'A' || c == 'B') {
                            ab = Optional.of(c);
                        }
                    }
                }
            }
        } catch (IOException ignored) {
            // leave defaults
        }
        return new CarConfig(features, ab);
    }

    private static String trim(String s) {
        int start = 0;
        int end = s.length();
        while (start < end && Character.isWhitespace(s.charAt(start))) {
            start++;
        }
        while (end > start && Character.isWhitespace(s.charAt(end - 1))) {
            end--;
        }
        return s.substring(start, end);
    }

    private record CarConfig(Set<String> featureExecNames, Optional<Character> abVariant) {}
}
