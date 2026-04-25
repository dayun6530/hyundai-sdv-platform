package com.sdv.killcontrol.kill;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/**
 * Writes KILL lines to the same {@code ExternalSignals_*.cmd} files the SDV platform reads.
 * Used synchronously from the REST controller so the HTTP response can wait for platform feedback.
 */
@Component
public class KillCommandWriter {

    private static final Logger log = LoggerFactory.getLogger(KillCommandWriter.class);

    private final Path externalSignalsDir;

    public KillCommandWriter() {
        Path repoRoot = resolveRepoRoot();
        this.externalSignalsDir = repoRoot.resolve("sdv-feature-flag-platform")
                .resolve("config")
                .resolve("signals");
    }

    private static Path resolveRepoRoot() {
        Path p = Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize();
        for (int i = 0; i < 8 && p != null; i++) {
            if (Files.isDirectory(p.resolve("sdv-feature-flag-platform"))) {
                return p;
            }
            p = p.getParent();
        }
        return Path.of(System.getProperty("user.dir")).toAbsolutePath().normalize();
    }

    public void appendKill(String carId, String command) {
        Path target = externalSignalsDir.resolve("ExternalSignals_" + carId + ".cmd");
        try {
            Files.createDirectories(target.getParent());
            Files.writeString(
                    target,
                    command + System.lineSeparator(),
                    StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.WRITE,
                    StandardOpenOption.APPEND);
            log.info("Wrote external signal: {} -> {}", command, target.toAbsolutePath());
        } catch (IOException e) {
            throw new IllegalStateException("Failed to write external signal file: " + target.toAbsolutePath(), e);
        }
    }
}
