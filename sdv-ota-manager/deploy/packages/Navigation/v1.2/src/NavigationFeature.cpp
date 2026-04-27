#include <iostream>
#include <string>
#include <vector>

// 실습용: 고정 경로를 순서대로 "안내"하는 아주 단순한 예시
int main() {
    const std::string domain = "Navigation";
    const std::string feature = "NavigationFeature";
    const std::string version = "1.2";

    std::cout << "[OTA 실습] " << feature << " v" << version << " (" << domain << ")\n";

    // 가짜 현재 위치 / 목적지
    std::cout << "현재: (37.5665, 126.9780)\n";
    std::cout << "목적: (37.5796, 126.9770)\n";

    std::vector<std::string> waypoints = {"출발", "교차로", "목적지 근처", "도착"};
    int step = 1;
    for (const auto& w : waypoints) {
        std::cout << "  단계 " << step++ << ": " << w << "\n";
    }

    std::cout << "안내 종료 (정상)\n";
    return 0;
}
