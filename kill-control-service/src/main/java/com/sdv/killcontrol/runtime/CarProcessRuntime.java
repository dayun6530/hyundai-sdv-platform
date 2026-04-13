package com.sdv.killcontrol.runtime;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * 프로세스 A/B 런타임 및 <strong>기존 그레이스풀 종료 로직</strong>이 있는 곳.
 * 웹 마이그레이션 시 본 클래스의 시작 로직·워커 본문·종료 절차는 요구사항상 변경하지 않음.
 * Signal 경로만 {@link com.sdv.killcontrol.signal.SignalReceiver}에서 {@link #shutdownAllGracefully()}를 호출하도록 바뀜.
 */
@Component
public class CarProcessRuntime {

    private static final Logger log = LoggerFactory.getLogger(CarProcessRuntime.class);
    private static final long JOIN_TIMEOUT_MS = 10_000L;

    private final ManagedLoop loopA = new ManagedLoop("Process-A");
    private final ManagedLoop loopB = new ManagedLoop("Process-B");

    @PostConstruct
    public void start() {
        loopA.start();
        loopB.start();
        log.info("CarProcessRuntime: A and B started");
    }

    @PreDestroy
    public void onContextDestroy() {
        shutdownAllGracefully();
    }

    /**
     * 기존 CMD KILL 처리 경로에서 호출하던 안전 종료 절차와 동일한 진입점.
     * (웹: {@link com.sdv.killcontrol.signal.SignalReceiver} → 본 메서드만 호출.)
     */
    public void shutdownAllGracefully() {
        log.warn("CarProcessRuntime: graceful shutdown requested for A and B");
        loopA.requestShutdown();
        loopB.requestShutdown();
        loopA.awaitQuietly(JOIN_TIMEOUT_MS);
        loopB.awaitQuietly(JOIN_TIMEOUT_MS);
    }

    private static final class ManagedLoop {
        private final String name;
        private final AtomicBoolean running = new AtomicBoolean(true);
        private Thread thread;

        private ManagedLoop(String name) {
            this.name = name;
        }

        void start() {
            thread = new Thread(this::run, name);
            thread.setDaemon(false);
            thread.start();
        }

        void run() {
            while (running.get() && !Thread.currentThread().isInterrupted()) {
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }

        void requestShutdown() {
            running.set(false);
            if (thread != null) {
                thread.interrupt();
            }
        }

        void awaitQuietly(long timeoutMs) {
            if (thread == null) {
                return;
            }
            try {
                thread.join(timeoutMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            if (thread.isAlive()) {
                log.warn("{} still alive after {} ms join", name, timeoutMs);
            }
        }
    }
}
