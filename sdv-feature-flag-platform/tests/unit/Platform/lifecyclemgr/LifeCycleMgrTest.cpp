#include "LifeCycleManager/LifeCycleMgr.h"
#include "TestAssert.h"

#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#define main lifecyclemgr_original_main
#include "src/Platform/LifeCycleManager/src/LifeCycleMgr.cpp"
#undef main

using namespace sdv::platform::lifecyclemgr;

int main() {
    sqa::test::TestCounter tc;
    LifeCycleMgr mgr;

    // Failure path: missing config should not throw.
    bool noThrowMissing = true;
    try {
        mgr.loadConfig("this_file_should_not_exist.conf");
    } catch (...) {
        noThrowMissing = false;
    }
    sqa::test::check(tc, noThrowMissing, "loadConfig missing file does not throw");

    // Success path: parse a valid static mockup config.
    const std::string validConfig = "tests/mockup/conf/lifecycle_valid.conf";
    bool noThrowValid = true;
    try {
        mgr.loadConfig(validConfig);
    } catch (...) {
        noThrowValid = false;
    }
    sqa::test::check(tc, noThrowValid, "loadConfig valid file does not throw");

    // Failure-style config: malformed lines should be ignored without exception.
    const std::string malformedConfig = "tests/mockup/conf/lifecycle_malformed.conf";
    bool noThrowMalformed = true;
    try {
        mgr.loadConfig(malformedConfig);
    } catch (...) {
        noThrowMalformed = false;
    }
    sqa::test::check(tc, noThrowMalformed, "loadConfig malformed lines do not throw");

    // Empty start/shutdown paths should be callable.
    bool noThrowStartShutdown = true;
    try {
        LifeCycleMgr emptyMgr;
        emptyMgr.start();
        emptyMgr.shutdown();
        emptyMgr.shutdown();
    } catch (...) {
        noThrowStartShutdown = false;
    }
    sqa::test::check(tc, noThrowStartShutdown, "start/shutdown are safely callable");

    // Spawn/monitor path: create mock executable in unit binary directory.
    char exePath[1024] = {0};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    std::string exeDir = ".";
    if (len > 0) {
        exePath[len] = '\0';
        std::string full(exePath);
        const auto pos = full.find_last_of('/');
        if (pos != std::string::npos) {
            exeDir = full.substr(0, pos);
        }
    }

    const std::string mockExec = exeDir + "/mock_feature_process";
    {
        std::ofstream out(mockExec);
        out << "#!/usr/bin/env bash\n";
        out << "sleep 1\n";
        out << "exit 0\n";
    }
    chmod(mockExec.c_str(), 0755);

    mkdir("tests/mockup/generated", 0755);
    const std::string spawnConfig = "tests/mockup/generated/lifecycle_spawn.conf";
    {
        std::ofstream out(spawnConfig);
        out << "address 127.0.0.1:50051\n";
        out << "communication mock_feature_process\n";
        out << "feature mock_feature_process\n";
    }

    bool noThrowSpawnMonitor = true;
    try {
        LifeCycleMgr spawnMgr;
        spawnMgr.loadConfig(spawnConfig);
        spawnMgr.start();
        spawnMgr.monitor();
        spawnMgr.shutdown();
    } catch (...) {
        noThrowSpawnMonitor = false;
    }
    sqa::test::check(tc, noThrowSpawnMonitor, "start/monitor/shutdown handle spawned process");

    return sqa::test::finalize(tc, "LifeCycleMgrTest");
}
