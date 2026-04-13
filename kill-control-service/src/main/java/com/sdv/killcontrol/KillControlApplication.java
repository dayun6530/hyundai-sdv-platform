package com.sdv.killcontrol;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableAsync;

@SpringBootApplication
@EnableAsync
public class KillControlApplication {

    public static void main(String[] args) {
        SpringApplication.run(KillControlApplication.class, args);
    }
}
