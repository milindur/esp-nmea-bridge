FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ARG ZEPHYR_SDK_VERSION=1.0.1
ARG DEBIAN_FRONTEND=noninteractive

ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr \
    ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION} \
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
    wget \
    xz-utils \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/zephyr-venv \
 && /opt/zephyr-venv/bin/pip install --upgrade pip setuptools wheel west \
 && chown -R vscode:vscode /opt/zephyr-venv

RUN wget -nv https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_gnu.tar.xz -O /tmp/zephyr-sdk.tar.xz \
 && mkdir -p /opt \
 && tar -xf /tmp/zephyr-sdk.tar.xz -C /opt \
 && rm /tmp/zephyr-sdk.tar.xz \
 && ${ZEPHYR_SDK_INSTALL_DIR}/setup.sh -t riscv64-zephyr-elf -t x86_64-zephyr-elf -h -c \
 && chown -R vscode:vscode ${ZEPHYR_SDK_INSTALL_DIR}

RUN groupadd -f plugdev \
 && usermod -aG dialout,plugdev vscode

USER vscode
WORKDIR /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge
