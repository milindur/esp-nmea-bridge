FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    VIRTUAL_ENV=/opt/zephyr-venv \
    PATH=/opt/zephyr-venv/bin:${PATH}

RUN apt-get update && apt-get install -y --no-install-recommends \
    bzip2 \
    ca-certificates \
    ccache \
    clangd \
    cmake \
    device-tree-compiler \
    dfu-util \
    file \
    g++ \
    g++-multilib \
    gcc \
    gcc-multilib \
    git \
    gperf \
    libmagic1 \
    libsdl2-dev \
    make \
    ninja-build \
    python3-dev \
    python3-pip \
    python3-venv \
    tio \
    udev \
    xz-utils \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/zephyr-venv \
 && /opt/zephyr-venv/bin/pip install --upgrade pip setuptools wheel west \
 && chown -R vscode:vscode /opt/zephyr-venv

RUN groupadd -f plugdev \
 && usermod -aG dialout,plugdev vscode

USER vscode
WORKDIR /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge
