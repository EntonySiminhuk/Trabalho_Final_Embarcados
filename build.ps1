Write-Host "🚀 Iniciando ambiente Zephyr..." -ForegroundColor Cyan

# Usa caminho relativo para ativar o ambiente virtual
& "..\.venv\Scripts\Activate.ps1"

# Avisa o Zephyr onde o SDK foi instalado
$Env:ZEPHYR_SDK_INSTALL_DIR = "D:\zephyr-sdk"

Write-Host "⚡ Compilando o código..." -ForegroundColor Green
west build -p always -b nucleo_g474re