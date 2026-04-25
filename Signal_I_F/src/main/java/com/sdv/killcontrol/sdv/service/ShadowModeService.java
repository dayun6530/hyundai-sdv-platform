package com.sdv.killcontrol.sdv.service;

import com.sdv.killcontrol.sdv.store.CommandStatus;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;

@Service
public class ShadowModeService {
    private final SdvControlTower controlTower;

    public ShadowModeService(SdvControlTower controlTower) {
        this.controlTower = controlTower;
    }

    public CompletableFuture<CommandStatus> setShadowMode(String vehicleId,
                                                          String runningFeature,
                                                          String shadowFeature,
                                                          String shadowVersion,
                                                          Duration timeout) {
        return controlTower.sendShadowMode(vehicleId, runningFeature, shadowFeature, shadowVersion, timeout);
    }
}

