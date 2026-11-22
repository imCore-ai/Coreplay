FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates file \
    qt6-base-dev qt6-base-dev-tools libgl-dev libegl-dev \
    libmpv-dev libopenimageio-dev openimageio-tools libopencolorio-dev \
    xvfb xauth mesa-utils libgl1-mesa-dri ffmpeg python3 gdb \
    && rm -rf /var/lib/apt/lists/*
