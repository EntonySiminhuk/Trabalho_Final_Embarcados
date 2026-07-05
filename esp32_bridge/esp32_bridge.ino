/*
 * Ponte do ESP32 para o projeto STM32 (Zephyr) -- DOIS papeis:
 *
 *  1) TELEMETRIA (principal): o STM32 envia periodicamente, pela USART1, uma
 *     linha "@V,<dc>,<h1>,<h2>,<h3>,<h4>,<anom>\n". O ESP faz o parse e um
 *     HTTP POST para a API do webapp (VibraMonitor):  POST /api/readings.
 *
 *  2) CLI UDP (legado, preservado): comandos de shell chegam por UDP:5000,
 *     sao encaminhados ao STM pela UART e a resposta volta por UDP.
 *
 *          PC  --UDP:5000-->  ESP32  --Serial2-->  STM32 (USART1)
 *          PC  <--UDP-------  ESP32  <--Serial2--  STM32
 *                              |
 *                              +--HTTP POST--> API .NET (webapp) :5080
 *
 * Como os dois fluxos compartilham a MESMA UART, o ESP le o Serial2 linha a
 * linha: linhas que comecam com "@V," sao telemetria (POST); as demais sao
 * saida do CLI e, se houver um pedido UDP pendente, viram a resposta.
 *
 * Ligacao fisica (crossover, 3V3 em ambos):
 *   ESP32 GPIO17 (TX2) --> STM32 PC5 (USART1_RX / D0)
 *   ESP32 GPIO16 (RX2) <-- STM32 PC4 (USART1_TX / D1)
 *   GND <-> GND comum.
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

/* =======================================================================
 *  CONFIGURACAO -- ajuste estes 3 blocos para o seu ambiente.
 * ======================================================================= */

/* 1) Wi-Fi (WPA2-PSK: hotspot ou roteador com senha unica).
 *    O driver do ESP nao precisa de Enterprise aqui; use uma rede PSK. */
static const char *WIFI_SSID = "Kety";
static const char *WIFI_PASS = "26092000";

/* 2) API do webapp (VibraMonitor). Coloque o IP do PC que roda o backend
 *    (.NET) na MESMA rede Wi-Fi. Porta padrao do backend: 5080.
 *    Descubra o IP do PC com `ipconfig` (IPv4 do adaptador Wi-Fi). */
static const char *API_HOST = "192.168.1.8";     /* IP do PC (Wi-Fi). Muda se trocar de rede -> confira com ipconfig */
static const uint16_t API_PORT = 5080;
static const char *DEVICE_ID = "STM32-01";       /* aparece no seletor do dashboard */

/* 3) Porta do CLI UDP (legado). */
static const uint16_t UDP_PORT = 5000;

/* ---- UART2 para o STM32 ---- */
static const int  STM_RX_PIN = 16;      /* ESP recebe (liga no TX do STM = PC4) */
static const int  STM_TX_PIN = 17;      /* ESP transmite (liga no RX do STM = PC5) */
static const long STM_BAUD   = 115200;  /* casar com a USART1 do STM (overlay) */

/* ---- Janela de coleta da resposta do CLI UDP ---- */
static const unsigned long RESP_IDLE_MS = 300;   /* silencio que encerra a resposta */
static const unsigned long RESP_MAX_MS  = 2000;  /* teto absoluto por comando */

/* ---- Timeouts do HTTP (nao travar o loop se a API sumir) ---- */
static const uint16_t HTTP_CONNECT_MS = 1000;
static const uint16_t HTTP_TIMEOUT_MS = 1000;

/* ---- Debug: 1 = logs detalhados de UDP/HTTP no Serial Monitor ---- */
#define VERBOSE 1

/* Backoff do POST: com a API fora, esperar progressivamente (ate 5 s) antes de
 * tentar de novo, para o loop NAO travar 1 s em 1 s e continuar servindo o CLI. */
static const unsigned long HTTP_BACKOFF_MAX_MS = 5000;

/* =======================================================================
 *  Estado
 * ======================================================================= */
WiFiUDP udp;
bool udp_started = false;

/* Acumulador de linha do Serial2 (vale tanto p/ telemetria quanto p/ CLI). */
static String rxLine;

/* Pedido de CLI UDP em andamento (aguardando resposta do STM). */
static bool          cli_pending   = false;
static IPAddress     cli_ip;
static uint16_t      cli_port      = 0;
static String        cli_resp;
static unsigned long cli_start     = 0;
static unsigned long cli_last_rx   = 0;

/* URL de ingestao montada uma vez. */
static String INGEST_URL;

/* Contadores simples para diagnostico via Serial. */
static unsigned long posts_ok = 0;
static unsigned long posts_fail = 0;

/* Estado do backoff do POST (ver HTTP_BACKOFF_MAX_MS). */
static unsigned long http_next_try_ms = 0;
static unsigned int  http_consec_fail = 0;

/* =======================================================================
 *  Wi-Fi
 * ======================================================================= */
static const char *wl_status_str(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE";
    case WL_NO_SSID_AVAIL:   return "SSID NAO ENCONTRADO";
    case WL_CONNECTED:       return "CONECTADO";
    case WL_CONNECT_FAILED:  return "FALHA (senha?)";
    case WL_CONNECTION_LOST:  return "CONEXAO PERDIDA";
    case WL_DISCONNECTED:    return "DESCONECTADO";
    default:                 return "OUTRO";
  }
}

bool connectWifi() {
  Serial.println("\nConectando a " + String(WIFI_SSID) + " ...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (millis() - start < 20000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi conectado. IP: " + WiFi.localIP().toString());
      return true;
    }
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nNao conectou. Status = " + String(wl_status_str(WiFi.status())));
  return false;
}

/* =======================================================================
 *  Telemetria: parse da linha "@V,..." e HTTP POST para a API
 * ======================================================================= */

/*
 * Faz o POST /api/readings com o JSON esperado pelo webapp (IngestDto).
 * A API deriva 'isAnomaly' (harmonic1st > limite), entao NAO enviamos a
 * contagem de anomalias no corpo -- ela vai so para log/debug.
 * accelX/Y/Z sao opcionais no DTO e ficam de fora (a telemetria carrega
 * apenas as harmonicas da FFT; ler o sensor aqui competiria pelo I2C).
 */
static bool postReading(float dc, float h1, float h2, float h3, float h4) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("POST pulado: WiFi status=%d (nao conectado)\n", WiFi.status());
    return false;
  }

  String json = "{";
  json += "\"deviceId\":\"" + String(DEVICE_ID) + "\",";
  json += "\"harmonicDc\":" + String(dc, 3) + ",";
  json += "\"harmonic1st\":" + String(h1, 3) + ",";
  json += "\"harmonic2nd\":" + String(h2, 3) + ",";
  json += "\"harmonic3rd\":" + String(h3, 3) + ",";
  json += "\"harmonic4th\":" + String(h4, 3);
  json += "}";

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, INGEST_URL)) {
    posts_fail++;
    Serial.println("HTTP begin() falhou (URL invalida?): " + INGEST_URL);
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  unsigned long t0 = millis();
  int code = http.POST(json);
  unsigned long dt = millis() - t0;
  http.end();

  if (code > 0 && code < 300) {
    posts_ok++;
    if (VERBOSE) {
      Serial.printf("POST ok: code=%d (%lums) ok=%lu fail=%lu\n",
                    code, dt, posts_ok, posts_fail);
    }
    return true;
  }

  /* Log detalhado do erro (o pedido do enunciado). */
  posts_fail++;
  Serial.printf("POST falhou: code=%d (%s) | WiFi=%d URL=%s dt=%lums ok=%lu fail=%lu\n",
                code, http.errorToString(code).c_str(), WiFi.status(),
                INGEST_URL.c_str(), dt, posts_ok, posts_fail);
  return false;
}

/*
 * Sonda /health no boot: prova de fogo da conectividade ESP->API. Se falhar
 * aqui, o problema e ambiente (bind localhost / IP errado / firewall), nao o
 * codigo de telemetria.
 */
static void httpHealthCheck() {
  String url = "http://" + String(API_HOST) + ":" + String(API_PORT) + "/health";
  Serial.println("Health check: GET " + url);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    Serial.println("  begin() falhou");
    return;
  }
  int code = http.GET();
  if (code > 0) {
    Serial.printf("  OK code=%d body=%s\n", code, http.getString().c_str());
  } else {
    Serial.printf("  FALHOU code=%d (%s) -- API inalcancavel a partir de %s\n"
                  "  Cheque: backend em 0.0.0.0:%u? API_HOST certo? firewall TCP %u?\n",
                  code, http.errorToString(code).c_str(),
                  WiFi.localIP().toString().c_str(), API_PORT, API_PORT);
  }
  http.end();
}

/* Recebe a linha ja sem o "\n" e sem o CR; espera o prefixo "@V,". */
static void handleTelemetryLine(const String &line) {
  /* line = "@V,dc,h1,h2,h3,h4,anom" */
  float v[6] = {0, 0, 0, 0, 0, 0};
  int idx = 0;
  int from = 3;                 /* pula o prefixo "@V," */
  int n = line.length();

  for (int i = from; i <= n && idx < 6; i++) {
    if (i == n || line[i] == ',') {
      v[idx++] = line.substring(from, i).toFloat();
      from = i + 1;
    }
  }

  if (idx < 5) {
    Serial.println("Telemetria malformada: " + line);
    return;
  }

  /* Backoff: se a API esta fora, nao tentar POST a cada amostra (nao bloquear
   * o loop). Descarta a amostra e tenta de novo apos http_next_try_ms. */
  unsigned long now = millis();
  if (now < http_next_try_ms) {
    if (VERBOSE) {
      Serial.printf("Telemetria descartada (backoff HTTP, faltam %lums)\n",
                    http_next_try_ms - now);
    }
    return;
  }

  /* v[0..4] = dc,h1,h2,h3,h4 ; v[5] = anomalias (so debug) */
  bool ok = postReading(v[0], v[1], v[2], v[3], v[4]);
  if (ok) {
    http_consec_fail = 0;
    http_next_try_ms = 0;
  } else {
    if (http_consec_fail < 20) {
      http_consec_fail++;
    }
    unsigned long backoff = 1000UL * http_consec_fail;   /* 1s, 2s, 3s... */
    if (backoff > HTTP_BACKOFF_MAX_MS) {
      backoff = HTTP_BACKOFF_MAX_MS;
    }
    http_next_try_ms = millis() + backoff;
  }
}

/* =======================================================================
 *  Uma linha completa chegou do Serial2: roteia telemetria x CLI
 * ======================================================================= */
static void handleSerialLine(String &line) {
  /* remove CR eventual do final */
  while (line.length() > 0 &&
         (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n')) {
    line.remove(line.length() - 1);
  }
  if (line.length() == 0) {
    return;
  }

  if (line.startsWith("@V,")) {
    handleTelemetryLine(line);         /* fluxo principal: vai para a API */
    return;
  }

  /* Caso contrario e saida do CLI de shell. So interessa se ha pedido UDP. */
  if (cli_pending) {
    cli_resp += line;
    cli_resp += '\n';
    cli_last_rx = millis();
  }
}

/* =======================================================================
 *  Setup / Loop
 * ======================================================================= */
void setup() {
  Serial.begin(115200);
  Serial2.begin(STM_BAUD, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
  delay(300);

  INGEST_URL = "http://" + String(API_HOST) + ":" + String(API_PORT) + "/api/readings";
  rxLine.reserve(128);
  cli_resp.reserve(512);

  Serial.println("\n=== Ponte ESP32: telemetria HTTP + CLI UDP ===");
  Serial.println("API de ingestao: " + INGEST_URL);

  if (connectWifi()) {
    /* Diagnostico de rede (estado Wi-Fi / rota / DNS). */
    Serial.println("IP="   + WiFi.localIP().toString() +
                   " GW="  + WiFi.gatewayIP().toString() +
                   " Mask=" + WiFi.subnetMask().toString() +
                   " DNS=" + WiFi.dnsIP().toString());
    Serial.printf("RSSI=%d dBm  WiFi.status=%d\n", WiFi.RSSI(), WiFi.status());

    udp.begin(UDP_PORT);
    udp_started = true;
    Serial.printf("CLI UDP:%u escutando em %s\n",
                  UDP_PORT, WiFi.localIP().toString().c_str());

    httpHealthCheck();   /* prova de fogo ESP->API antes de rodar */
  }
}

void loop() {
  /* 1) Mantem o Wi-Fi vivo (reconexao automatica). */
  if (WiFi.status() != WL_CONNECTED) {
    udp_started = false;
    if (connectWifi()) {
      udp.begin(UDP_PORT);
      udp_started = true;
      Serial.printf("CLI UDP:%u pronto em %s\n",
                    UDP_PORT, WiFi.localIP().toString().c_str());
    } else {
      delay(3000);
      return;
    }
  }

  /* 2) Consome o Serial2 linha a linha (telemetria e/ou resposta do CLI). */
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      handleSerialLine(rxLine);
      rxLine = "";
    } else {
      rxLine += c;
      if (rxLine.length() > 250) {   /* guarda-chuva contra linha sem fim */
        rxLine = "";
      }
    }
  }

  /* 3) Fecha um pedido de CLI UDP quando a resposta "assentou". */
  if (cli_pending) {
    unsigned long now = millis();
    if ((now - cli_last_rx >= RESP_IDLE_MS) || (now - cli_start >= RESP_MAX_MS)) {
      if (cli_resp.length() == 0) {
        cli_resp = "(sem resposta do STM)\n";
      }
      int sent = udp.beginPacket(cli_ip, cli_port);
      udp.write((const uint8_t *)cli_resp.c_str(), cli_resp.length());
      int ended = udp.endPacket();  /* 1 = ok, 0 = falha ao enfileirar */
      Serial.printf("UDP TX para %s:%u -> %u bytes (beginPacket=%d endPacket=%d)\n",
                    cli_ip.toString().c_str(), cli_port,
                    (unsigned)cli_resp.length(), sent, ended);
      cli_pending = false;
      cli_resp = "";
    }
  }

  /* 4) Novo comando de CLI por UDP? (so aceita um por vez) */
  if (udp_started && !cli_pending) {
    int pkt = udp.parsePacket();
    if (pkt > 0) {
      char cmd[256];
      int len = udp.read(cmd, sizeof(cmd) - 1);
      if (len < 0) {
        len = 0;
      }
      cmd[len] = '\0';
      while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r')) {
        cmd[--len] = '\0';
      }
      if (len > 0) {
        cli_ip   = udp.remoteIP();
        cli_port = udp.remotePort();
        Serial.printf("UDP RX de %s:%u -> \"%s\" (%d bytes) -> encaminhado ao STM\n",
                      cli_ip.toString().c_str(), cli_port, cmd, len);
        Serial2.print(cmd);
        Serial2.print('\n');

        cli_pending = true;
        cli_resp    = "";
        cli_start   = millis();
        cli_last_rx = millis();
      }
    }
  }
}
