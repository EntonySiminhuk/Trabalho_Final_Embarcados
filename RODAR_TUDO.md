# Guia completo para rodar o sistema (hardware → webapp)

Passo a passo do zero: fiação das placas, subir o webapp, gravar os firmwares e testar.
IPs deste ambiente: **PC = 192.168.1.8** · **ESP32 = 192.168.1.17** · **Gateway = 192.168.1.1**
(se trocar de rede, confira com `ipconfig` no PC e no Serial do ESP — ver seção "O que varia").

---

## 1. Fiação das placas

### 1.1 STM32 ↔ ESP32 (UART, crossover, 3V3 nos dois)
| STM32 (Nucleo-G474RE) | Liga em | ESP32 |
|---|---|---|
| **PC4** = D1 (USART1_TX) | → | **GPIO16** (RX2) |
| **PC5** = D0 (USART1_RX) | ← | **GPIO17** (TX2) |
| **GND** | ↔ | **GND** (comum, obrigatório) |

> Crossover: TX de um vai no RX do outro. O GND comum é essencial.

### 1.2 Sensor MPU6050 ↔ STM32 (I2C1, endereço 0x68)
| MPU6050 | STM32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | SCL do I2C1 |
| SDA | SDA do I2C1 |

> Confirme a detecção depois pelo shell local do STM: `i2c scan I2C_1` deve listar `0x68`.

### 1.3 Alimentação
- STM32: cabo USB no **ST-Link** (também é o console local, VCP).
- ESP32: cabo USB próprio (também é o Serial Monitor).
- Podem ser duas USBs diferentes — o GND comum do item 1.1 mantém as referências alinhadas.

---

## 2. Subir o webapp (3 terminais no PC)

### Terminal 1 — Banco (Postgres via Docker)
```powershell
cd D:\Projetos\respos\Faculdade\zephyrproject\Trabalho_Final_Embarcados\webapp
docker compose up -d postgres
docker ps        # "vibra_postgres" deve ficar "healthy"
```

### Terminal 2 — Backend (.NET) — **deixe aberto, não cole mais nada aqui**
```powershell
cd D:\Projetos\respos\Faculdade\zephyrproject\Trabalho_Final_Embarcados\webapp\backend\VibraMonitor.Api
$env:ASPNETCORE_ENVIRONMENT = "Development"
dotnet run --no-launch-profile --urls "http://0.0.0.0:5080"
```
✅ Tem que aparecer: `Now listening on: http://0.0.0.0:5080` (0.0.0.0, **não** localhost).

> `--no-launch-profile` + `--urls` são obrigatórios: sem eles o `launchSettings.json`
> força `localhost` e o ESP não conecta (dá `POST code=-1 connection refused`).

### Terminal 3 — Frontend (dashboard)
```powershell
cd D:\Projetos\respos\Faculdade\zephyrproject\Trabalho_Final_Embarcados\webapp\frontend
npm install      # só na 1ª vez
npm run dev
```
Abra **http://localhost:5173**.

### (1ª vez apenas) Firewall — libera a porta 5080 na rede
```powershell
# PowerShell como Administrador, uma única vez:
New-NetFirewallRule -DisplayName "VibraMonitor API 5080" -Direction Inbound -Protocol TCP -LocalPort 5080 -Action Allow
```

### Confirme a rede (num 4º terminal, opcional) — os DOIS têm que responder
```powershell
curl http://localhost:5080/health
curl http://192.168.1.8:5080/health     # se este falhar, o ESP também falha
```

---

## 3. Gravar os firmwares

### 3.1 STM32 (Zephyr) — telemetria + CLI
```powershell
cd D:\Projetos\respos\Faculdade\zephyrproject\Trabalho_Final_Embarcados
.\build.ps1      # compila
.\flash.ps1      # grava (feche o PuTTY antes, senão dá "open failed")
```
No console local (115200) deve surgir: `Telemetria STM->ESP ativa (1000 ms...)`.

### 3.2 ESP32 (Arduino IDE)
Confira no topo de `esp32_bridge/esp32_bridge.ino` (o que **varia** — ver seção 6):
```cpp
WIFI_SSID = "Kety";   WIFI_PASS = "26092000";   // sua rede PSK
API_HOST  = "192.168.1.8";                       // IP do PC
DEVICE_ID = "STM32-01";
```
Grave pelo Arduino IDE (board ESP32, porta COM certa) e abra o **Serial Monitor a 115200**.

✅ Esperado no boot do ESP:
```
IP=192.168.1.17 GW=192.168.1.1 ...
Health check: GET http://192.168.1.8:5080/health
  OK code=200 body={"status":"ok"...}
CLI UDP:5000 escutando em 192.168.1.17
```
Depois, a cada ~1 s: `POST ok: code=201`.

---

## 4. Testar (dashboard em tempo real)

1. Abra **http://localhost:5173**.
2. No seletor de dispositivos, escolha **`STM32-01`** — KPIs e gráficos andando a ~1 Hz.
3. Sacuda/vibre o sensor com força → 1ª harmônica passa de 100 → cartão de **anomalia** acende.
4. Aperte o **botão do STM32** → zera o contador local de anomalias.

> O device `SIM-01` (simulador) pode aparecer junto — é normal, é só o gerador de teste.
> Para ver só o hardware, selecione `STM32-01` no dashboard.

---

## 5. Testar o CLI UDP (funciona em paralelo com a telemetria)

```powershell
cd D:\Projetos\respos\Faculdade\zephyrproject\Trabalho_Final_Embarcados
.\udp_cli.ps1 192.168.1.17          # IP do ESP32
```
Comandos disponíveis (digite e Enter):
| Comando | O que faz |
|---|---|
| `show_fft` | Harmônicas atuais da FFT + status de anomalia |
| `show_raw` | Aceleração instantânea X/Y/Z + módulo |
| `show_anomalies` | Total de anomalias acumuladas |
| `reset_anomalies` | Zera o histórico de anomalias |
| `rt_status` | Info das tarefas de tempo real |
| `exit` | Sai do CLI |

A resposta chega em <3 s mesmo com a telemetria rodando.

---

## 6. O que VARIA (para saber caso dê erro)

| Item | Onde | Muda quando |
|---|---|---|
| `API_HOST` (IP do PC) | `esp32_bridge.ino` | Trocar de rede Wi-Fi / DHCP novo → `ipconfig` no PC |
| IP do ESP (`192.168.1.17`) | arg do `udp_cli.ps1` | Trocar de rede → veja no Serial do ESP (linha `IP=`) |
| `WIFI_SSID` / `WIFI_PASS` | `esp32_bridge.ino` | Trocar de rede (tem que ser **PSK**, não Enterprise) |
| Porta COM do ESP | Arduino IDE | Trocar de USB/porta |
| Portas 5544/5080/5173/5000 | fixas | não mudam |

### Erros comuns → causa → ação
| Erro | Causa | Ação |
|---|---|---|
| ESP: `POST code=-1 connection refused` | backend em `localhost` ou fora | Terminal 2 com `--no-launch-profile --urls 0.0.0.0`; conferir `curl 192.168.1.8:5080/health` |
| `curl 192.168.1.8:5080` falha (localhost ok) | bind errado / firewall | rever Terminal 2 + regra de firewall |
| Backend cai com erro de Postgres | Docker/Postgres off | Terminal 1 (`docker ps` healthy) |
| ESP: `Health ... FALHOU` | `API_HOST` errado ou API fora | conferir IP do PC e o Terminal 2 |
| CLI UDP "sem resposta" | IP do ESP errado / STM sem responder | conferir IP no Serial; testar `show_fft` no console local do STM |
| STM `.\flash.ps1` "open failed" | PuTTY/serial aberto na porta | feche o PuTTY e regrave |
| Dashboard sem `STM32-01` | POSTs falhando | siga a linha do `code=-1` acima |

---

## 7. Sequência rápida (quando já estiver tudo configurado)

1. Ligar as duas placas (USB) — já cabeadas.
2. Terminal 1: `docker compose up -d postgres`
3. Terminal 2: `dotnet run --no-launch-profile --urls "http://0.0.0.0:5080"`
4. Terminal 3: `npm run dev`
5. Abrir http://localhost:5173 → selecionar `STM32-01`.
6. (Opcional) `.\udp_cli.ps1 192.168.1.17` para o CLI.
