# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    PATH=/opt/venv/bin:$PATH \
    QT_QPA_PLATFORM=offscreen \
    PYTHONDONTWRITEBYTECODE=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential gcc-multilib g++-multilib cmake ninja-build git ca-certificates \
        qtbase5-dev qtdeclarative5-dev fonts-dejavu-core python3-venv python3-dev libcairo2 \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /opt/venv \
    && pip install --no-cache-dir 'pip>=26.2' \
    && pip install --no-cache-dir pytest 'Pillow>=13.0.0' numpy cairosvg zxing-cpp \
        platformio build pip-audit

WORKDIR /work/qrkude
