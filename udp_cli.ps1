# Cliente UDP para o CLI externo do STM (via ponte ESP32).
# Uso:  .\udp_cli.ps1               (usa o IP padrao abaixo)
#       .\udp_cli.ps1 192.168.137.184
#
# O PC precisa estar na MESMA rede Wi-Fi do ESP.
# Digite comandos (show_fft, show_raw, show_anomalies). "exit" para sair.

param(
    [string]$Ip = "192.168.1.8",
    [int]$Port = 5000
)

# Socket NAO conectado: envia para o destino e aceita resposta de qualquer origem.
$udp = New-Object System.Net.Sockets.UdpClient
$udp.Client.ReceiveTimeout = 3000

# Desliga o SIO_UDP_CONNRESET (evita SocketException por ICMP "port unreachable").
try {
    $SIO_UDP_CONNRESET = [int]([uint32]0x9800000C)
    [void]$udp.Client.IOControl($SIO_UDP_CONNRESET, [byte[]]@(0,0,0,0), $null)
} catch { }

$dest = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse($Ip), $Port)

Write-Host "Enviando para $Ip`:$Port (UDP). Comandos: show_fft, show_raw, show_anomalies. 'exit' p/ sair." -ForegroundColor Cyan

while ($true) {
    $cmd = Read-Host "cli"
    if ([string]::IsNullOrWhiteSpace($cmd)) { continue }
    if ($cmd -eq "exit") { break }

    $bytes = [Text.Encoding]::ASCII.GetBytes($cmd)
    try {
        [void]$udp.Send($bytes, $bytes.Length, $dest)
    } catch {
        Write-Host "(falha ao enviar: $($_.Exception.Message))" -ForegroundColor Red
        continue
    }

    try {
        $anyEp = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $data = $udp.Receive([ref]$anyEp)
        Write-Host ("[resposta de " + $anyEp.Address + ":" + $anyEp.Port + "]") -ForegroundColor DarkGray
        Write-Host ([Text.Encoding]::ASCII.GetString($data))
    } catch {
        Write-Host "(sem resposta - provavel firewall do Windows bloqueando UDP de entrada)" -ForegroundColor Yellow
    }
}

$udp.Close()
