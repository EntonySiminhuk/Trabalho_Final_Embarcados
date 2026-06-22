#!/bin/bash

echo "🔓 A liberar as permissões de segurança USB no Linux..."
sudo chmod -R 777 /dev/bus/usb/

echo "🚀 A ligar o ambiente Zephyr..."
source ~/zephyr-venv/bin/activate
source ~/zephyrproject/zephyr/zephyr-env.sh

echo "⚡ A gravar na placa Nucleo..."
west flash --runner openocd