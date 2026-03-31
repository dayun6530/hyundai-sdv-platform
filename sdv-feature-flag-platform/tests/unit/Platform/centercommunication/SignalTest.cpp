#include "CenterCommunication/Signal.h"
#include "TestAssert.h"

#include <filesystem>
#include <fstream>

using namespace sdv::platform::centercommunication;

namespace {

class TestSignal final : public Signal {
public:
    TestSignal(const std::string& centerUrl, const std::string& path)
        : Signal(centerUrl, path) {}

    void start() override { running_ = true; }
    void stop() override { running_ = false; }

    bool callHttpPost(const std::string& endpoint, const std::string& body) {
        return httpPost(endpoint, body);
    }

    std::string callHttpGet(const std::string& endpoint) {
        return httpGet(endpoint);
    }

    void callWrite(const SignalData& data) { writeToFile(data); }
    std::vector<SignalData> callReadAndClear() { return readAndClearFile(); }
};

}  // namespace

int main() {
    sqa::test::TestCounter tc;
    namespace fs = std::filesystem;
    fs::create_directories("tests/mockup/generated");

    const std::string filePath = "tests/mockup/generated/SignalTest.cmd";
    std::ofstream(filePath, std::ios::trunc).close();

    SignalData s1{"FLAG_ON", "ADAS", "on", std::chrono::system_clock::now()};
    SignalData s2 = SignalData::deserialize("KILL:NAVI:stop");
    sqa::test::check(tc, s1.serialize() == "FLAG_ON:ADAS:on", "SignalData serialize works");
    sqa::test::check(tc,
                     s2.type == "KILL" && s2.target == "NAVI" && s2.payload == "stop",
                     "SignalData deserialize full format works");

    SignalData s3 = SignalData::deserialize("INVALID");
    sqa::test::check(tc, s3.type == "INVALID", "SignalData deserialize fallback works");

    TestSignal signal("http://center.example.com", filePath);
    sqa::test::check(tc, !signal.isRunning(), "Signal starts in stopped state");
    signal.start();
    sqa::test::check(tc, signal.isRunning(), "Signal start toggles running state");
    signal.stop();
    sqa::test::check(tc, !signal.isRunning(), "Signal stop toggles running state");

    const bool postOk = signal.callHttpPost("/api/signals", "body");
    const std::string getResult = signal.callHttpGet("/api/signals");
    sqa::test::check(tc, postOk, "Signal httpPost prototype returns true");
    sqa::test::check(tc, getResult.empty(), "Signal httpGet prototype returns empty string");

    signal.callWrite(SignalData{"FLAG_OFF", "EV", "off", std::chrono::system_clock::now()});
    signal.callWrite(SignalData{"KILL", "NAVI", "stop", std::chrono::system_clock::now()});
    auto rows = signal.callReadAndClear();
    sqa::test::check(tc, rows.size() == 2, "Signal file write/read returns two rows");

    std::ifstream in(filePath);
    std::string line;
    std::getline(in, line);
    sqa::test::check(tc, line.empty(), "Signal readAndClear truncates file");

    return sqa::test::finalize(tc, "SignalTest");
}
