package com.sdv.killcontrol.sdv.service;

import com.sdv.killcontrol.sdv.store.CommandStatus;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;

@Service
public class KillSwitchService {
    private final SdvControlTower controlTower;

    public KillSwitchService(SdvControlTower controlTower) {
        this.controlTower = controlTower;
    }

    public CompletableFuture<CommandStatus> kill(String vehicleId, String targetFeature, Duration timeout) {
        return controlTower.sendKillSwitch(vehicleId, targetFeature, timeout);
    }
}

