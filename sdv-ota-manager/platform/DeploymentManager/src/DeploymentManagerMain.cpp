#include "DeploymentManager/CampaignManager.h"
#include "DeploymentManager/CampaignRunManager.h"
#include "DeploymentManager/CampaignDocument.h"
#include "DeploymentManager/DeploymentManager.h"
#include "DeploymentManager/StateManager.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

    void usage() {
        std::cout
            << "DeploymentMgr — file-based deploy helper\n"
            << "  Use the repository root (sdv-feature-flag-platform) as cwd so deploy/ and config/ resolve "
            "like ./build/car_1.\n"
            << "  deploy = deployment stage, select/rollback/sync-config = release stage.\n"
            << "\n"
            << "  DeploymentMgr [--config <path>] <command> [args]\n"
            << "\n"
            << "Commands:\n"
            << "  deploy <ExecName>     Deployment: packages → staging → active\n"
            << "  list                  Show deployment/release status, selection, active binaries, history\n"
            << "  status                Show the same status view plus key file paths\n"
            << "  select <DOM> <A|B>    Release: update selection + config + activation health check\n"
            << "  rollback <DOM>        Release rollback to previous stable selection\n"
            << "  sync-config           Release helper: apply deploy/active_selection.conf to config only\n"
            << "  sync-config-car <id>  Per-car: rewrite config/car/<id>.conf feature block from deploy/state/<id>.json (car_1..car_10)\n"
            << "  fleet-selftest        Fleet deploy self-test (VehicleRegistry + VersionManager + StateManager)\n"
            << "  apply-pending-native-car <car_id> [--feature ADAS|Navigation|Energy]\n"
            << "                      After a deferred fleet-deploy: copy packages → active for pending features, then promote pending→active in deploy/state.\n"
            << "                      Optional --feature limits apply to one fleet feature (campaign promote uses this).\n"
            << "                      Invoked automatically at car platform start when SDV_PLATFORM_DEPLOY_ROOT is set.\n"
            << "  fleet-deploy <SCOPE> <TARGET...> <FEATURE> <ROLE|LEGACY_SLOT> <VERSION> [--sync-config] [--no-defer-native] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n"
            << "                      ROLE: primary|candidate (legacy internal slot A|B also accepted)\n"
            << "                      SCOPE=vehicle|group|region|group_region\n"
            << "                      vehicle: <car_id>\n"
            << "                      group: <group>\n"
            << "                      region: <region>\n"
            << "                      group_region: <group> <region>\n"
            << "                      Optional trailing --sync-config: after successful state deploy, sync config/car/<id>.conf\n"
            << "                      vehicle only: default deferred native rollout (pending_active_*; active unchanged until apply-pending-native-car / restart). --no-defer-native restores immediate OTA-safety-gated active update.\n"
            << "                      Optional --docker-runtime[=candidate|promote]: after successful fleet deploy, run ota_vehicle.sh deploy-shadow (candidate) or promote (primary).\n"
            << "                      Promote requires an existing candidate container; it does not run deploy-shadow in the same command.\n"
            << "                      Default: local image inspect only (no pull). Optional --docker-pull[=missing|always] requires --docker-runtime.\n"
            << "  fleet-set-approval <car_id> <true|false>\n"
            << "  fleet-rollback vehicle <car_id> <FEATURE> [--target-version <vX.Y>] [--sync-config]\n"
            << "                      FEATURE=ADAS|Navigation|Energy; car_id=car_1..car_10\n"
            << "                      Default: restores deploy/state from deploy/rollback/<car>/<FEATURE>.json.\n"
            << "                      Optional --target-version: restore active_version from feature.rollback_stack (state-based metadata).\n"
            << "                      Optional --sync-config: rewrite config/car/<id>.conf from state\n"
            << "  fleet-flag vehicle <car_id> <FEATURE> <ON|OFF> [--sync-config]\n"
            << "                      Administrative feature flag in deploy/state only (not Kill Switch / not Shadow).\n"
            << "                      Optional --sync-config: rewrite config/car/<id>.conf from state (active_slot block; flag may not map to a .conf line).\n"
            << "  fleet-kill vehicle <car_id> <FEATURE> <reason> [--sync-config]\n"
            << "                      Emergency kill: kill_switch + kill_reason + per-feature fallback (not fleet-flag; not Shadow).\n"
            << "                      reason is a non-empty token (e.g. lane_detection_error). Optional --sync-config syncs .conf from state after kill.\n"
            << "  fleet-shadow vehicle <car_id> <FEATURE> <ON|OFF>\n"
            << "                      Records shadow mode in deploy/state (shadow_slot B + B version from version_map when ON). No config sync (--sync-config not supported).\n"
            << "  fleet-promote-shadow vehicle <car_id> <FEATURE>\n"
            << "                      Metadata-only: promote Shadow -> Primary in deploy/state, push previous primary to rollback_stack (cap 3), disable Shadow.\n"
            << "                      Does NOT execute docker containers (use scripts/ota_vehicle.sh promote for runtime).\n"
            << "  fleet-ack <car_id> <FEATURE> <ACK_STATUS> <message...> [--version <vX.Y>] [--deploy-id <id>]\n"
            << "                      Record a vehicle ACK into deploy/state/<car>.json (file-based; no MQTT/HTTP in this step).\n"
            << "  campaign-validate <campaign_json_path>\n"
            << "                      Read-only: validate campaign JSON, linked rollout policy, package/manifest,\n"
            << "                      version_map, target vehicles, and runtime preconditions (no deploy, no state writes).\n"
            << "  campaign-run <campaign_json_path> --phase candidate|promote\n"
            << "                      candidate: fleet-deploy + Docker deploy-shadow (pull=missing when configured).\n"
            << "                      promote: fleet-deploy + Docker promote (no pull; candidate container must exist).\n"
            << "\n"
            << "Terminology notes (compatibility):\n"
            << "  - In deploy/state: active_* is legacy alias for primary_* (Docker runtime terminology).\n"
            << "  - A/B slot is a legacy version_map selector; it is NOT the same concept as Shadow.\n"
            << "\n"
            << "Default config: config/SdvPlatform.conf (override with --config, e.g. config/car/car_1.conf)\n";
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
    if (cmd == "sync-config-car") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr sync-config-car <car_id>\n";
            return 1;
        }
        std::string msg;
        const bool ok = dm.syncCarConfigFromState(positional[1], msg);
        dm.emitSummaryJsonWithFleetHistory(dm.buildSyncConfigCarSummaryJson(positional[1], ok, msg));
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-selftest") {
        return dm.selfTestFleetDeploy() ? 0 : 1;
    }
    if (cmd == "fleet-set-approval") {
        if (positional.size() < 3) {
            std::cerr << "Usage: DeploymentMgr fleet-set-approval <car_id> <true|false>\n";
            return 1;
        }
        const std::string car_id = positional[1];
        const std::string v = positional[2];
        const bool approved = (v == "true" || v == "1" || v == "yes" || v == "on");
        sdv::platform::deployment::StateManager sm(".");
        std::string err;
        if (!sm.updateDriverApproval(car_id, approved, err)) {
            std::cerr << "[FLEET] set approval FAILED: " << (err.empty() ? "unknown" : err) << "\n";
            return 1;
        }
        std::cout << "[FLEET] set approval OK car_id=" << car_id << " driver_approved=" << (approved ? "true" : "false") << "\n";
        return 0;
    }
    if (cmd == "fleet-ack") {
        if (positional.size() < 5) {
            std::cerr << "Usage: DeploymentMgr fleet-ack <car_id> <FEATURE> <ACK_STATUS> <message...> [--version <vX.Y>] [--deploy-id <id>]\n";
            return 1;
        }
        const std::string car_id = positional[1];
        const std::string feature = positional[2];
        const std::string ack_status = positional[3];

        std::string version;
        std::string deploy_id;
        std::vector<std::string> msgParts;
        for (std::size_t i = 4; i < positional.size(); ++i) {
            const std::string a = positional[i];
            if (a == "--version" && i + 1 < positional.size()) {
                version = positional[++i];
                continue;
            }
            if (a == "--deploy-id" && i + 1 < positional.size()) {
                deploy_id = positional[++i];
                continue;
            }
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown fleet-ack option: " << a << "\n";
                return 1;
            }
            msgParts.push_back(a);
        }
        std::string message;
        for (std::size_t i = 0; i < msgParts.size(); ++i) {
            if (i) message.push_back(' ');
            message += msgParts[i];
        }

        sdv::platform::deployment::StateManager sm(".");
        std::string err;
        const bool ok = sm.recordAck(car_id, feature, ack_status, message, version, deploy_id, err);
        if (!ok) {
            std::cerr << "[FLEET] ack FAILED: " << (err.empty() ? "unknown" : err) << "\n";
        } else {
            std::cout << "[FLEET] ack OK car_id=" << car_id << " feature=" << feature
                      << " status=" << ack_status << "\n";
        }

        // Minimal summary JSON for automation/log scraping (status-server parses [SUMMARY_JSON]).
        // Keep this local (no DeploymentManager API changes) and backward-compatible.
        auto jsonEscape = [](const std::string& s) {
            std::string out;
            out.reserve(s.size() + 8);
            for (const char c : s) {
                switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(c); break;
                }
            }
            return out;
        };
        std::cout << "[SUMMARY_JSON] {"
                  << "\"action\":\"fleet-ack\","
                  << "\"car_id\":\"" << jsonEscape(car_id) << "\","
                  << "\"feature\":\"" << jsonEscape(feature) << "\","
                  << "\"ack_status\":\"" << jsonEscape(ack_status) << "\","
                  << "\"version\":\"" << jsonEscape(version) << "\","
                  << "\"deploy_id\":\"" << jsonEscape(deploy_id) << "\","
                  << "\"message\":\"" << jsonEscape(message) << "\","
                  << "\"result\":\"" << (ok ? "success" : "failed") << "\","
                  << "\"reason\":\"" << jsonEscape(err) << "\""
                  << " }\n";
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-rollback") {
        std::vector<std::string> fr;
        fr.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            fr.emplace_back(positional[i]);
        }
        bool sync_config = false;
        std::string target_version;
        while (true) {
            if (!fr.empty() && fr.back() == "--sync-config") {
                sync_config = true;
                fr.pop_back();
                continue;
            }
            if (fr.size() >= 2 && fr[fr.size() - 2] == "--target-version") {
                target_version = fr.back();
                fr.pop_back();
                fr.pop_back();
                continue;
            }
            break;
        }
        for (const auto& a : fr) {
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown or misplaced fleet-rollback option: " << a
                          << " (supported trailing options: --sync-config, --target-version <vX.Y>)\n";
                return 1;
            }
        }
        if (fr.empty()) {
            std::cerr << "Usage: DeploymentMgr fleet-rollback vehicle <car_id> <FEATURE> [--target-version <vX.Y>] [--sync-config]\n";
            return 1;
        }
        if (fr[0] != "vehicle") {
            sdv::platform::deployment::DeploymentManager::FleetRollbackSummary sum;
            sum.scope = fr[0];
            sum.result = "failed";
            sum.reason = "fleet-rollback: scope \"" + fr[0] + "\" is not supported yet (only vehicle)";
            dm.printFleetRollbackSummary(sum);
            dm.emitSummaryJsonWithFleetHistory(dm.fleetRollbackSummaryToJson(sum));
            std::cerr << sum.reason << "\n";
            return 1;
        }
        if (fr.size() != 3) {
            std::cerr << "Usage: DeploymentMgr fleet-rollback vehicle <car_id> <FEATURE> [--target-version <vX.Y>] [--sync-config]\n";
            return 1;
        }
        sdv::platform::deployment::DeploymentManager::FleetRollbackSummary sum;
        std::string err;
        const bool ok = dm.fleetRollbackVehicle(fr[1], fr[2], sync_config, sum, err, target_version);
        dm.printFleetRollbackSummary(sum);
        dm.emitSummaryJsonWithFleetHistory(dm.fleetRollbackSummaryToJson(sum));
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-flag") {
        std::vector<std::string> ff;
        ff.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            ff.emplace_back(positional[i]);
        }
        bool sync_config = false;
        while (!ff.empty() && ff.back() == "--sync-config") {
            sync_config = true;
            ff.pop_back();
        }
        for (const auto& a : ff) {
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown or misplaced fleet-flag option: " << a
                          << " (only trailing --sync-config is supported)\n";
                return 1;
            }
        }
        if (ff.empty()) {
            std::cerr << "Usage: DeploymentMgr fleet-flag vehicle <car_id> <FEATURE> <ON|OFF> [--sync-config]\n";
            return 1;
        }
        if (ff[0] != "vehicle") {
            sdv::platform::deployment::DeploymentManager::FleetFlagSummary sum;
            sum.scope = ff[0];
            sum.result = "failed";
            sum.reason = "fleet-flag: scope \"" + ff[0] + "\" is not supported yet (only vehicle)";
            dm.printFleetFlagSummary(sum);
            dm.emitSummaryJsonWithFleetHistory(dm.fleetFlagSummaryToJson(sum));
            std::cerr << sum.reason << "\n";
            return 1;
        }
        if (ff.size() != 4) {
            std::cerr << "Usage: DeploymentMgr fleet-flag vehicle <car_id> <FEATURE> <ON|OFF> [--sync-config]\n";
            return 1;
        }
        sdv::platform::deployment::DeploymentManager::FleetFlagSummary sum;
        std::string err;
        const bool ok = dm.fleetFlagVehicle(ff[1], ff[2], ff[3], sync_config, sum, err);
        dm.printFleetFlagSummary(sum);
        dm.emitSummaryJsonWithFleetHistory(dm.fleetFlagSummaryToJson(sum));
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-kill") {
        std::vector<std::string> fk;
        fk.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            fk.emplace_back(positional[i]);
        }
        bool sync_config = false;
        while (!fk.empty() && fk.back() == "--sync-config") {
            sync_config = true;
            fk.pop_back();
        }
        for (const auto& a : fk) {
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown or misplaced fleet-kill option: " << a
                          << " (only trailing --sync-config is supported)\n";
                return 1;
            }
        }
        if (fk.empty()) {
            std::cerr << "Usage: DeploymentMgr fleet-kill vehicle <car_id> <FEATURE> <reason> [--sync-config]\n";
            return 1;
        }
        if (fk[0] != "vehicle") {
            sdv::platform::deployment::DeploymentManager::FleetKillSummary sum;
            sum.scope = fk[0];
            sum.result = "failed";
            sum.reason = "fleet-kill: scope \"" + fk[0] + "\" is not supported yet (only vehicle)";
            dm.printFleetKillSummary(sum);
            dm.emitSummaryJsonWithFleetHistory(dm.fleetKillSummaryToJson(sum));
            std::cerr << sum.reason << "\n";
            return 1;
        }
        if (fk.size() != 4) {
            std::cerr << "Usage: DeploymentMgr fleet-kill vehicle <car_id> <FEATURE> <reason> [--sync-config]\n";
            return 1;
        }
        sdv::platform::deployment::DeploymentManager::FleetKillSummary sum;
        std::string err;
        const bool ok = dm.fleetKillVehicle(fk[1], fk[2], fk[3], sync_config, sum, err);
        dm.printFleetKillSummary(sum);
        dm.emitSummaryJsonWithFleetHistory(dm.fleetKillSummaryToJson(sum));
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-shadow") {
        std::vector<std::string> sh;
        sh.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            sh.emplace_back(positional[i]);
        }
        for (const auto& a : sh) {
            if (a == "--sync-config") {
                std::cerr
                    << "fleet-shadow: --sync-config is not supported for fleet-shadow because active_slot/active_version "
                       "are unchanged; shadow state is deploy/state only and does not require per-car .conf sync.\n";
                return 1;
            }
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown fleet-shadow option: " << a << "\n";
                return 1;
            }
        }
        if (sh.empty()) {
            std::cerr << "Usage: DeploymentMgr fleet-shadow vehicle <car_id> <FEATURE> <ON|OFF>\n";
            return 1;
        }
        if (sh[0] != "vehicle") {
            sdv::platform::deployment::DeploymentManager::FleetShadowSummary sum;
            sum.scope = sh[0];
            sum.result = "failed";
            sum.reason = "fleet-shadow: scope \"" + sh[0] + "\" is not supported yet (only vehicle)";
            dm.printFleetShadowSummary(sum);
            dm.emitSummaryJsonWithFleetHistory(dm.fleetShadowSummaryToJson(sum));
            std::cerr << sum.reason << "\n";
            return 1;
        }
        if (sh.size() != 4) {
            std::cerr << "Usage: DeploymentMgr fleet-shadow vehicle <car_id> <FEATURE> <ON|OFF>\n";
            return 1;
        }
        sdv::platform::deployment::DeploymentManager::FleetShadowSummary sum;
        std::string err;
        const bool ok = dm.fleetShadowVehicle(sh[1], sh[2], sh[3], sum, err);
        dm.printFleetShadowSummary(sum);
        dm.emitSummaryJsonWithFleetHistory(dm.fleetShadowSummaryToJson(sum));
        return ok ? 0 : 1;
    }
    if (cmd == "fleet-promote-shadow") {
        std::vector<std::string> fp;
        fp.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            fp.emplace_back(positional[i]);
        }
        if (fp.size() != 3 || fp[0] != "vehicle") {
            std::cerr << "Usage: DeploymentMgr fleet-promote-shadow vehicle <car_id> <FEATURE>\n";
            return 1;
        }
        std::string msg;
        const bool ok = dm.fleetPromoteShadowVehicle(fp[1], fp[2], msg);
        std::cout << (ok ? "[fleet-promote-shadow] " : "[fleet-promote-shadow] ERROR: ") << msg << "\n";
        dm.emitSummaryJsonWithFleetHistory(std::string("{\"action\":\"fleet-promote-shadow\",\"scope\":\"vehicle\",\"car_id\":\"") +
            fp[1] + "\",\"feature\":\"" + fp[2] + "\",\"ok\":" + (ok ? "true" : "false") +
            ",\"message\":\"" + msg + "\"}");
        return ok ? 0 : 1;
    }
    if (cmd == "apply-pending-native-car") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr apply-pending-native-car <car_id> [--feature ADAS|Navigation|Energy]\n";
            return 1;
        }
        std::string onlyFeature;
        for (std::size_t i = 2; i < positional.size(); ++i) {
            if (positional[i] == "--feature" && i + 1 < positional.size()) {
                onlyFeature = positional[i + 1];
                ++i;
            }
        }
        std::string msg;
        const bool ok = dm.applyPendingNativeForCar(positional[1], msg, onlyFeature);
        if (!msg.empty()) {
            std::cout << (ok ? "[apply-pending-native-car] " : "[apply-pending-native-car] ERROR: ") << msg << "\n";
        }
        return ok ? 0 : 1;
    }
    if (cmd == "campaign-validate") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr campaign-validate <campaign_json_path>\n";
            usage();
            return 1;
        }
        sdv::platform::deployment::CampaignValidator validator(".");
        return validator.validateCampaignFile(positional[1]);
    }
    if (cmd == "campaign-run") {
        if (positional.size() < 2) {
            std::cerr << "Usage: DeploymentMgr campaign-run <campaign_json_path> --phase candidate|promote\n";
            usage();
            return 1;
        }
        std::string phase;
        for (std::size_t i = 2; i < positional.size(); ++i) {
            const std::string& a = positional[i];
            if (a == "--phase" && i + 1 < positional.size()) {
                phase = positional[++i];
                continue;
            }
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown campaign-run option: " << a << "\n";
                return 1;
            }
        }
        sdv::platform::deployment::CampaignRunManager runner(".");
        return runner.runCampaignFile(positional[1], phase, dm);
    }
    if (cmd == "fleet-deploy") {
        if (positional.size() < 6) {
            std::cerr << "Usage: DeploymentMgr fleet-deploy <vehicle|group|region|group_region> <target...> <FEATURE> <SLOT> <VERSION> [--sync-config] [--no-defer-native] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
            return 1;
        }
        std::vector<std::string> fd;
        fd.reserve(positional.size());
        for (std::size_t i = 1; i < positional.size(); ++i) {
            fd.emplace_back(positional[i]);
        }
        bool sync_config = false;
        bool defer_native_vehicle = true;
        sdv::platform::deployment::DeploymentManager::FleetDockerRuntimeOptions docker_runtime;
        for (;;) {
            if (fd.empty()) {
                break;
            }
            if (fd.back() == "--sync-config") {
                sync_config = true;
                fd.pop_back();
                continue;
            }
            if (fd.back() == "--no-defer-native") {
                defer_native_vehicle = false;
                fd.pop_back();
                continue;
            }
            if (fd.back() == "--docker-pull") {
                docker_runtime.pull_policy = "missing";
                fd.pop_back();
                continue;
            }
            const std::string dockerPullPrefix = "--docker-pull=";
            if (fd.back().size() >= dockerPullPrefix.size()
                && fd.back().compare(0, dockerPullPrefix.size(), dockerPullPrefix) == 0) {
                std::string policy = fd.back().substr(dockerPullPrefix.size());
                if (policy.empty()) {
                    policy = "missing";
                }
                docker_runtime.pull_policy = policy;
                fd.pop_back();
                continue;
            }
            if (fd.back() == "--docker-runtime") {
                docker_runtime.requested = true;
                docker_runtime.mode = "candidate";
                fd.pop_back();
                continue;
            }
            const std::string dockerPrefix = "--docker-runtime=";
            if (fd.back().size() >= dockerPrefix.size()
                && fd.back().compare(0, dockerPrefix.size(), dockerPrefix) == 0) {
                docker_runtime.requested = true;
                docker_runtime.mode = fd.back().substr(dockerPrefix.size());
                if (docker_runtime.mode.empty()) {
                    docker_runtime.mode = "candidate";
                }
                fd.pop_back();
                continue;
            }
            break;
        }
        for (const auto& a : fd) {
            if (!a.empty() && a[0] == '-') {
                std::cerr << "Unknown or misplaced fleet-deploy option: " << a
                          << " (supported trailing: --sync-config, --no-defer-native, --docker-runtime[=candidate|promote], --docker-pull[=missing|always])\n";
                return 1;
            }
        }
        if (!docker_runtime.pull_policy.empty() && !docker_runtime.requested) {
            std::cerr << "fleet-deploy: --docker-pull requires --docker-runtime\n";
            return 1;
        }
        if (!docker_runtime.pull_policy.empty()) {
            std::string policy = docker_runtime.pull_policy;
            for (char& c : policy) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (policy != "missing" && policy != "always") {
                std::cerr << "fleet-deploy: invalid --docker-pull policy \"" << docker_runtime.pull_policy
                          << "\" (expected missing or always)\n";
                return 1;
            }
            docker_runtime.pull_policy = policy;
        }
        if (docker_runtime.requested) {
            std::string runtimeMode = docker_runtime.mode;
            for (char& c : runtimeMode) {
                if (c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            if (runtimeMode != "candidate" && runtimeMode != "promote") {
                std::cerr << "fleet-deploy: invalid --docker-runtime mode \"" << docker_runtime.mode
                          << "\" (expected candidate or promote)\n";
                return 1;
            }
            docker_runtime.mode = runtimeMode;
        }
        if (fd.size() < 5) {
            std::cerr << "Usage: DeploymentMgr fleet-deploy <vehicle|group|region|group_region> <target...> <FEATURE> <ROLE|LEGACY_SLOT> <VERSION> [--sync-config] [--no-defer-native] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
            return 1;
        }
        const std::size_t slotIdx = (fd[0] == "group_region") ? 4 : 3;
        const std::string slotNorm = sdv::platform::deployment::normalizeFleetDeploySlotArg(fd[slotIdx]);
        if (slotNorm.empty()) {
            std::cerr << "fleet-deploy: invalid deployment role: expected primary or candidate\n";
            std::cerr << "fleet-deploy: (internal legacy slot must be A or B; got \"" << fd[slotIdx] << "\")\n";
            return 1;
        }
        fd[slotIdx] = slotNorm;
        const std::string& scope = fd[0];
        sdv::platform::deployment::DeploymentManager::DeploySummary sum;
        if (scope == "vehicle") {
            if (fd.size() != 5) {
                std::cerr << "Usage: DeploymentMgr fleet-deploy vehicle <car_id> <FEATURE> <SLOT> <VERSION> [--sync-config] [--no-defer-native] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
                return 1;
            }
            sum = dm.deployToVehicleWithSummary(fd[1], fd[2], fd[3], fd[4], sync_config, defer_native_vehicle,
                docker_runtime.requested ? &docker_runtime : nullptr);
        } else if (scope == "group") {
            if (fd.size() != 5) {
                std::cerr << "Usage: DeploymentMgr fleet-deploy group <group> <FEATURE> <SLOT> <VERSION> [--sync-config] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
                return 1;
            }
            sum = dm.deployToGroupWithSummary(fd[1], fd[2], fd[3], fd[4], sync_config,
                docker_runtime.requested ? &docker_runtime : nullptr);
        } else if (scope == "region") {
            if (fd.size() != 5) {
                std::cerr << "Usage: DeploymentMgr fleet-deploy region <region> <FEATURE> <SLOT> <VERSION> [--sync-config] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
                return 1;
            }
            sum = dm.deployToRegionWithSummary(fd[1], fd[2], fd[3], fd[4], sync_config,
                docker_runtime.requested ? &docker_runtime : nullptr);
        } else if (scope == "group_region") {
            if (fd.size() != 6) {
                std::cerr << "Usage: DeploymentMgr fleet-deploy group_region <group> <region> <FEATURE> <SLOT> <VERSION> [--sync-config] [--docker-runtime[=candidate|promote]] [--docker-pull[=missing|always]]\n";
                return 1;
            }
            sum = dm.deployToGroupAndRegionWithSummary(fd[1], fd[2], fd[3], fd[4], fd[5], sync_config,
                docker_runtime.requested ? &docker_runtime : nullptr);
        } else {
            std::cerr << "Invalid scope: " << scope << " (expected vehicle|group|region|group_region)\n";
            return 1;
        }

        dm.printFleetDeploySummary(sum);
        // Consider "total=0" a failure for automation, but still return 0 when nothing failed.
        if (sum.total == 0) return 1;
        if (sum.failed != 0) return 1;
        if (sum.sync_config_requested) {
            for (const auto& r : sum.results) {
                if (r.success && r.config_sync_requested && !r.config_synced) {
                    return 1;
                }
            }
        }
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    usage();
    return 1;
}
