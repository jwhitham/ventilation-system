#!/bin/bash -xe

mkdir -p build
cd build
cmake -DPICO_BOARD=pico2_w \
    -DPICO_SDK_PATH=../../pico-sdk \
    -DWIFI_SETTINGS_REMOTE=2 \
    ..


