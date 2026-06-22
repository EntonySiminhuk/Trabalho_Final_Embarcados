#!/bin/bash

echo "🚀 A ligar o ambiente Zephyr para compilação..."
source ~/zephyr-venv/bin/activate
source ~/zephyrproject/zephyr/zephyr-env.sh

echo " A compilar o código..."
# O comando abaixo compila o código da pasta atual apagando o cache antigo
west build -p always -b nucleo_g474re