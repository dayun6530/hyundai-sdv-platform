#include "DeploymentManager/DeploymentManager.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage() {
    std::cout
        << "DeploymentMgr — file-based deploy helper\n"
        << "  Use the repository root (sdv-feature-flag-platform) as cwd so deploy/ and config/ resolve "
           "like ./build/car_A.\n"
        << "\n"
        << "  DeploymentMgr [--config <path>] <command> [args]\n"
        << "\n"
        << "Commands:\n"
        << "  deploy <ExecName>     Copy deploy/packages/<ExecName> → deploy/active/\n"
        << "  list                    Show active binaries + recent manifest lines\n"
        << "  status                  Show active_selection.conf + paths\n"
        << "  select <DOM> <A|B>     DOM = ADAS | NAV | ENERGY — update selection + config\n"
        << "  rollback <DOM>         Restore previous active version for domain\n"
        << "  sync-config            Apply deploy/active_selection.conf to config only\n"
        << "\n"
        << "Default config: config/SdvPlatform.conf (override with --config, e.g. config/car_A.conf)\n";
}

std::string defaultConfigPath() {
    return "config/SdvPlatform.conf";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath = defaultConfigPath();
    std::vector<char*> positional;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            positional.push_back(argv[i]);
        }
    }

    if (positional.empty()) {
        usage();
        return 1;
    }

    const std::string cmd = positional[0];
    sdv::platform::deployment::DeploymentManager dm(".");

    if (cmd == "deploy") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr deploy <ExecName>\n";
            return 1;
        }
        return dm.deployExecutable(positional[1]) ? 0 : 1;
    }
    if (cmd == "list") {
        dm.printInventory();
        return 0;
    }
    if (cmd == "status") {
        dm.printStatus(configPath);
        return 0;
    }
    if (cmd == "select") {
        if (positional.size() < 3) {
            std::cerr << "Usage: DeploymentMgr select <ADAS|NAV|ENERGY> <A|B>\n";
            return 1;
        }
        return dm.selectVersion(positional[1], positional[2], configPath) ? 0 : 1;
    }
    if (cmd == "rollback") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr rollback <ADAS|NAV|ENERGY>\n";
            return 1;
        }
        return dm.rollbackDomain(positional[1], configPath) ? 0 : 1;
    }
    if (cmd == "sync-config") {
        return dm.syncConfig(configPath) ? 0 : 1;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    usage();
    return 1;
}
