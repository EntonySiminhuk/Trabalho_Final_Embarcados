Write-Host "🔧 Abrindo menu de configuração..." -ForegroundColor Magenta

# Usa caminho relativo para ativar o ambiente virtual
& "..\.venv\Scripts\Activate.ps1"
$Env:ZEPHYR_SDK_INSTALL_DIR = "D:\zephyr-sdk"

west build -t menuconfig -d .\build