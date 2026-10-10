
# 🚗 SDV Feature Flag Platform

**현대자동차 연계 팀 프로젝트 | SDV 기반 차량 소프트웨어 기능 제어 및 관리 플랫폼**

차량 소프트웨어를 Feature 단위로 관리하고, 기능의 실행 상태와 생명주기를 제어하기 위한 플랫폼입니다.

본 저장소는 전체 SDV OTA 프로젝트 중 C++ 기반 Feature Flag 플랫폼 구현을 중심으로 관리합니다. 전체 프로젝트에서는 React, Node.js/Express, Spring Boot, MQTT, Docker 등을 함께 활용했습니다.

## ✨ Key Features

- **Feature Lifecycle Management** — 차량 기능의 실행, 종료 및 상태 관리
- **Feature Flag** — 설정 및 신호 기반 기능 활성화·비활성화
- **Kill Switch** — 이상 상황 발생 시 기능 중단 제어
- **EventBus** — Feature 간 이벤트 기반 통신
- **WatchDog** — 차량 기능 실행 상태 모니터링

## 🛠️ Tech Stack

**Core**

![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)

**Communication**

![gRPC](https://img.shields.io/badge/gRPC-244C5A?style=for-the-badge&logo=google&logoColor=white)
![Protocol Buffers](https://img.shields.io/badge/Protocol_Buffers-4285F4?style=for-the-badge&logo=google&logoColor=white)

**Environment**

![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

## 📂 Repository Structure

```text
sdv-feature-flag-platform/
├── src/
│   ├── Feature/       # 차량 기능 구현
│   └── Platform/      # 기능 관리 및 통신
├── config/            # 플랫폼 설정
├── tests/             # 테스트 코드
├── Docs/              # 프로젝트 자료
└── markdown/          # 설계 및 기술 문서
```

## 📚 Documentation

- [Architecture](./sdv-feature-flag-platform/markdown/ARCHITECTURE.md)
- [Coding Conventions](./sdv-feature-flag-platform/Docs/CONVENTIONS.md)
- [Original Team Repository](https://github.com/hyundai-sdv-platform/feature-flag-platform)

## 👥 Team

**현대자동차 연계 SDV 팀 프로젝트**

| Member | GitHub |
|---|---|
| 이다윤 | [@dayun6530](https://github.com/dayun6530) |
| 정윤호 | [@Yuno-Jung](https://github.com/Yuno-Jung) |
| 구자중 | [@wkwndrn](https://github.com/wkwndrn) |
