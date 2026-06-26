# start_env.ps1
$Env:ZEPHYR_SDK_INSTALL_DIR = "D:\zephyr-sdk"
& "..\.venv\Scripts\Activate.ps1"
Write-Host "Ambiente Zephyr ativado com sucesso!" -ForegroundColor Green