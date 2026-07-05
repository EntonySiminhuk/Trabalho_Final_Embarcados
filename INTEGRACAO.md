# Integração Sensores → STM32 → ESP32 → WebApp

Documento da integração entre o firmware embarcado (STM32 + ESP32) e o
dashboard **VibraMonitor** (`webapp/`). O dashboard **não foi alterado** —
apenas os firmwares e a configuração de execução.

---

## 1. Diagrama do fluxo completo

```
                     I2C                Zbus                USART1 (115200 8N1)
  ┌─────────┐  ~1kHz  ┌──────────────────────────┐  "@V,..."  ┌──────────┐
  │ MPU6050 ├────────►│  STM32 (Zephyr 4.4)       ├───────────►│  ESP32   │
  │ (accel) │         │                           │  1 Hz      │ (Arduino)│
  └─────────┘         │  fft_task (Hard RT, p3) ──┼─┐          └────┬─────┘
                      │    FFT 256 + IIR          │ │ publica       │ Wi-Fi
                      │  send_result_task (p3) ───┼─┤ Zbus          │ HTTP POST
                      │    anomalias              │ │               ▼
                      │  telemetry_task (p10) ◄───┘ │        ┌─────────────────┐
                      │    lê Zbus, serializa ──────┘        │  API .NET :5080 │
                      │  uart_cli (p6) ◄── CLI UDP (legado)  │  /api/readings  │
                      └───────────────────────────┘         │      │ SignalR   │
                                                             │      ▼           │
                                                             │  PostgreSQL      │
                                                             └──────┬───────────┘
                                                                    ▼
                                                        Dashboard React :5173
                                                        (tempo real via SignalR)
```

Fluxo paralelo preservado (entrega anterior):

```
  PC (udp_cli.ps1) ──UDP:5000──► ESP32 ──USART1──► STM32 (shell) ──► resposta
```

---

## 2. Lista das alterações realizadas

### STM32 (Zephyr) — novo caminho de *push* periódico

| Arquivo | Alteração |
|---|---|
| `src/vibration.h` | **Novo.** Header compartilhado: `struct fft_msg`, `ZBUS_CHAN_DECLARE(fft_data_chan)`, `anomaly_count`, `anomaly_reset()`. |
| `src/bridge_uart.h` | **Novo.** Declara `bridge_uart_write()` (escrita atômica na USART1). |
| `src/telemetry.c` | **Novo.** Thread de **baixa prioridade** (p10) que lê o último espectro no Zbus e emite `"@V,<dc>,<h1>,<h2>,<h3>,<h4>,<anom>\n"` na USART1 a cada **1 s**. |
| `src/uart_cli.c` | `uart_write_str` (privada) → `bridge_uart_write` (pública, com `K_MUTEX`). Serializa a TX entre CLI e telemetria. |
| `src/zbus.c` | Passa a incluir `vibration.h` (struct/canal deixaram de ser locais). Lógica inalterada. |
| `CMakeLists.txt` | Adiciona `src/telemetry.c` às fontes. |
| `prj.conf` | Adiciona `CONFIG_CBPRINTF_FP_SUPPORT=y` (float no `snprintk`). |

### ESP32 (Arduino) — encaminhamento para a API

| Arquivo | Alteração |
|---|---|
| `esp32_bridge/esp32_bridge.ino` | Reescrito. Passa a ler a UART **linha a linha**: linhas `"@V,"` → `HTTP POST /api/readings`; demais linhas → resposta do CLI UDP (preservado). Adiciona `HTTPClient`, config de `API_HOST/PORT/DEVICE_ID`, timeouts e contadores. Removido o código de Wi-Fi Enterprise (não usado). |

### WebApp — **zero alteração de código**

O contrato `IngestDto` já casa 1:1 com o `fft_msg`. Recomendação operacional
(não é mudança de código): desligar o simulador em
`webapp/backend/VibraMonitor.Api/appsettings.json` (`"Simulator":{"Enabled":false}`)
quando o hardware estiver conectado. Se deixá-lo ligado, o dashboard mostra
`SIM-01` **e** `STM32-01` como dispositivos separados no seletor — ambos funcionam.

---

## 3. Justificativa técnica das decisões

- **Por que uma thread nova de baixa prioridade (p10)?** O requisito pede um
  *push* periódico que **não** interfira nas tarefas críticas. `fft_task` e
  `send_result_task` são p3 (Hard/Soft RT); o CLI é p6. Com p10 (número maior =
  menos prioritária no Zephyr), a telemetria só roda quando o resto está ocioso,
  e a I/O lenta da UART fica fora do caminho do DSP.

- **Por que 1 Hz?** O dashboard é um monitor de *tendência*; 1 Hz basta, casa com
  a taxa que o simulador do webapp já usava (`IntervalMs=1000`) e mantém o tráfego
  Wi-Fi/HTTP e a carga da UART irrisórios. A detecção de anomalia continua em
  tempo real **no STM** (p3), independente dessa taxa.

- **Por que reaproveitar o Zbus em vez de ler o sensor na thread nova?** O
  espectro já é publicado por `fft_task`. Ler o MPU6050 novamente na thread de
  telemetria disputaria o barramento I2C com o Hard RT. Então a telemetria só
  **consome** o canal (`zbus_chan_read`) — sem tocar no sensor.

- **Por que uma linha com sentinela `"@V,"` e mutex na UART?** A USART1 é
  compartilhada com o CLI UDP. O prefixo permite ao ESP separar telemetria de
  respostas de shell; o `K_MUTEX` em `bridge_uart_write` garante que uma linha
  não seja intercalada byte a byte com a outra.

- **Por que HTTP POST (e não UDP) para o webapp?** É o que a API espera
  (`POST /api/readings`, `IngestDto`). Reusa toda a stack existente
  (persistência + SignalR + dashboard) sem tocar no webapp.

- **Por que omitir `accelX/Y/Z` no POST?** São opcionais no DTO e não estão no
  canal Zbus (que carrega só as harmônicas). Incluí-los exigiria ler o sensor na
  thread de telemetria — ver acima. Fácil de adicionar depois se desejado.

---

## 4. Estratégia de tratamento de erros e perda de comunicação

| Camada | Falha | Tratamento |
|---|---|---|
| STM→ESP (UART) | Sem espectro novo no Zbus | `telemetry_task` faz `LOG_WRN` e pula o ciclo (não envia lixo). |
| STM→ESP (UART) | TX concorrente CLI × telemetria | `K_MUTEX` serializa; linhas nunca se misturam. |
| ESP telemetria | Linha `"@V,"` malformada | `handleTelemetryLine` valida ≥5 campos; descarta e loga se inválida. |
| ESP→API (HTTP) | API fora do ar / timeout | `HTTPClient` com connect/timeout de 1,5 s; falha é logada, **próxima amostra em 1 s** (perda tolerável, sem travar o loop). Contadores `posts_ok/fail`. |
| ESP↔Wi-Fi | Queda de Wi-Fi | `loop()` detecta `!= WL_CONNECTED` e **reconecta** automaticamente; POST é *no-op* enquanto desconectado. |
| WebApp | Reconexão do dashboard | SignalR `withAutomaticReconnect()` (já existente, não alterado). |
| CLI UDP | Sem resposta do STM | Janela `RESP_IDLE_MS`/`RESP_MAX_MS`; devolve `"(sem resposta do STM)"`. |

**Nota:** a telemetria é *fire-and-forget* (não há buffer de reenvio). Para um
monitor de tendência a 1 Hz isso é adequado; se exigirem entrega garantida,
o passo seguinte seria um buffer local no ESP + `POST /api/readings/batch`.

---

## 5. Instruções de teste e validação

### 5.1 Pré-checagem sem hardware (só webapp)
1. Suba banco + backend + frontend (ver `webapp/COMO_RODAR.md`).
2. Com o simulador ligado, confirme o dashboard atualizando (`SIM-01`). Isso valida a base.
3. Teste o endpoint manualmente:
   ```powershell
   curl -X POST http://localhost:5080/api/readings `
     -H "Content-Type: application/json" `
     -d '{"deviceId":"STM32-01","harmonicDc":510,"harmonic1st":145.5,"harmonic2nd":40,"harmonic3rd":12,"harmonic4th":6}'
   ```
   O dispositivo `STM32-01` deve aparecer no seletor e `isAnomaly=true` (h1>100).

### 5.2 STM32
1. Compilar e gravar:
   ```powershell
   .\build.ps1
   .\flash.ps1
   ```
2. No console (VCP do ST-Link), confirmar o log:
   `Telemetria STM->ESP ativa (1000 ms, sentinela "@V,")`.
3. `show_fft` no shell local deve bater com os valores enviados.

### 5.3 ESP32
1. Em `esp32_bridge.ino`, ajustar `WIFI_SSID/PASS`, `API_HOST` (IP do PC na
   rede Wi-Fi, via `ipconfig`) e `DEVICE_ID`.
2. Gravar pelo Arduino IDE; abrir o Serial Monitor (115200).
3. Esperado: `WiFi conectado`, `API de ingestao: http://<ip>:5080/api/readings`
   e, a cada ~1 s, POSTs sem erro (sem linhas `POST falhou`).

### 5.4 Ponta a ponta
1. Banco + backend + frontend no ar; simulador **desligado** (opcional).
2. STM e ESP ligados e cabeados (crossover PC4↔GPIO16, PC5↔GPIO17, GND comum).
3. No dashboard (`http://localhost:5173`): device `STM32-01`, KPIs e gráficos
   avançando a ~1 Hz, indicador "tempo real" verde.
4. Induzir vibração acima do limite → 1ª harmônica > 100 → cartão de anomalia
   acende; pressionar o botão do STM zera o contador local de anomalias.
5. CLI legado ainda funciona em paralelo:
   ```powershell
   .\udp_cli.ps1 <ip_do_esp>
   # show_fft, show_raw, show_anomalies, reset_anomalies
   ```

### 5.4.1 ⚠️ Pré-requisito de rede (causa comum de `POST code=-1`)
O backend .NET, por padrão, escuta **só em `localhost`** (`launchSettings.json`),
inacessível para o ESP. Para o hardware alcançá-lo:
```powershell
# 1) Suba a API ouvindo em TODAS as interfaces (não só loopback):
cd webapp/backend/VibraMonitor.Api
dotnet run --urls "http://0.0.0.0:5080"

# 2) Libere a porta no firewall do Windows (uma vez, como admin):
New-NetFirewallRule -DisplayName "VibraMonitor API 5080" -Direction Inbound `
  -Protocol TCP -LocalPort 5080 -Action Allow

# 3) Descubra o IPv4 do PC na rede Wi-Fi e use-o em API_HOST no .ino:
ipconfig    # ex.: 192.168.1.x
```
Valide de outro dispositivo na mesma rede (ex.: celular): abrir
`http://<IP_do_PC>:5080/health` deve responder `{"status":"ok"}`. Se só
`http://localhost:5080/health` funciona no PC, o bind ainda está em loopback.

### 5.5 Testes de falha
- Derrubar o backend → ESP loga `POST falhou`, dashboard congela; ao voltar, retoma sozinho.
- Desligar/religar o Wi-Fi → ESP reconecta e volta a postar.
- Rodar `show_fft` pelo CLI UDP enquanto a telemetria roda → resposta chega
  íntegra (linhas `@V,` filtradas), provando a coexistência na UART.
