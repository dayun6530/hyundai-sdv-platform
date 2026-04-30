# Ubuntu 22.04 (jammy) 기본 저장소에는 libgrpc++.so.1.51 이 없어서,
# WSL/최신 툴체인에서 링크된 gRPC C++ 바이너리는 noble + libgrpc++1.51t64 가 맞는 경우가 많습니다.
# (반드시 jammy 이미지가 필요하면: 멀티스테이지로 빌드 호스트의 .so 를 복사하거나, 호스트와 동일 OS로 맞추세요.)
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libc6 \
        libstdc++6 \
        libgcc-s1 \
        libatomic1 \
        zlib1g \
        libssl3t64 \
        libcurl4t64 \
        libcares2 \
        libre2-10 \
        libgrpc++1.51t64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY AdasFeature /app/AdasFeature
RUN chmod +x /app/AdasFeature
