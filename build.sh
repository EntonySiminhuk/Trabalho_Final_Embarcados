#!/bin/bash

cd ~/zephyrproject
# source .venv/bin/activate
source ~/zephyr-venv/bin/activate
west build -o=-j16 -p always ~/zephyr_workspace/zephyr_app -d ~/zephyr_workspace/zephyr_app/build
