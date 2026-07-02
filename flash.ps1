Write-Host "🚀 Iniciando ambiente Zephyr..." -ForegroundColor Cyan

# Usa caminho relativo para ativar o ambiente virtual
& "..\.venv\Scripts\Activate.ps1"

# Avisa o Zephyr onde o SDK foi instalado
$Env:ZEPHYR_SDK_INSTALL_DIR = "D:\zephyr-sdk"

Write-Host "⚡ Gravando na placa Nucleo (OpenOCD via ST-Link)..." -ForegroundColor Yellow
# Runner openocd (vem no Zephyr SDK) — dispensa o STM32CubeProgrammer.
west flash --runner openocd