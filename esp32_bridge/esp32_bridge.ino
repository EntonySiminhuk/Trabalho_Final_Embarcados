/*
 * Ponte Wi-Fi <-> UART para o projeto STM32 (Zephyr).
 *
 *   PC  --UDP:5000-->  ESP32  --Serial2-->  STM32 (USART1 PB6/PB7)
 *   PC  <---UDP-------  ESP32  <--Serial2--  STM32
 *
 * Ligacao fisica (crossover, 3V3 em ambos):
 *   ESP32 GPIO17 (TX2) --> STM32 PB7 (USART1_RX)
 *   ESP32 GPIO16 (RX2) <-- STM32 PB6 (USART1_TX)
 *   GND <-> GND comum.
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_eap_client.h"

/* =======================================================================
 *  ESCOLHA A REDE:
 *    1 = WPA2-Enterprise (rede da faculdade: usuario + senha)
 *    0 = WPA2-PSK        (hotspot / roteador com senha unica)
 * ======================================================================= */
#define USE_ENTERPRISE 0  

#if USE_ENTERPRISE
  const char *ssid         = "UTFPR-ALUNO";
  /*
   * Identidade e usuario: comece com o login "puro" (sem dominio).
   * Se falhar, teste as variantes comentadas.
   */
  const char *eap_identity = "a2420244";
  const char *eap_username = "a2420244";
  const char *eap_password = "entony123";
  /* Variantes de identidade para testar se nao conectar:
   *   "a2420244@alunos.utfpr.edu.br"
   *   "a2420244@aluno.utfpr.edu.br"
   */
#else
  const char *ssid     = "Kety";
  const char *password = "26092000";
#endif

/* ---- Porta do CLI UDP ---- */
static const uint16_t UDP_PORT = 5000;

/* ---- UART2 para o STM32 ---- */
static const int  STM_RX_PIN = 16;      /* ESP recebe (liga no TX do STM = PB6) */
static const int  STM_TX_PIN = 17;      /* ESP transmite (liga no RX do STM = PB7) */
static const long STM_BAUD   = 115200;  /* casar com a USART1 do STM (overlay) */

static const unsigned long RESP_IDLE_MS = 300;
static const unsigned long RESP_MAX_MS  = 2000;

WiFiUDP udp;
bool udp_started = false;

static const char *wl_status_str(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:    return "IDLE";
    case WL_NO_SSID_AVAIL:  return "SSID NAO ENCONTRADO";
    case WL_CONNECTED:      return "CONECTADO";
    case WL_CONNECT_FAILED: return "FALHA (usuario/senha/identidade?)";
    case WL_CONNECTION_LOST:return "CONEXAO PERDIDA";
    case WL_DISCONNECTED:   return "DESCONECTADO";
    default:                return "OUTRO";
  }
}

bool connectWifi() {
  Serial.println("\nConectando a " + String(ssid) + " ...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);
  WiFi.setSleep(false);

#if USE_ENTERPRISE
  /*
   * WPA2-Enterprise (PEAP / MSCHAPv2), API do core 3.x.
   * IMPORTANTE: NAO configurar CA cert -> o cliente nao valida o servidor
   * e a conexao passa. Configurar um CA invalido faz travar nos "....".
   */
  esp_eap_client_set_identity((const uint8_t *)eap_identity, strlen(eap_identity));
  esp_eap_client_set_username((const uint8_t *)eap_username, strlen(eap_username));
  esp_eap_client_set_password((const uint8_t *)eap_password, strlen(eap_password));
  esp_wifi_sta_enterprise_enable();
  WiFi.begin(ssid);                 /* sem senha aqui: a auth e via EAP */
#else
  WiFi.begin(ssid, password);
#endif

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

void setup() {
  Serial.begin(115200);
  Serial2.begin(STM_BAUD, SERIAL_8N1, STM_RX_PIN, STM_TX_PIN);
  delay(300);
  Serial.println("\n=== Ponte ESP32 UDP<->UART ===");

  if (connectWifi()) {
    udp.begin(UDP_PORT);
    udp_started = true;
    Serial.printf("Ponte UDP:%u pronta. Use no PC:  nc -u %s %u\n",
                  UDP_PORT, WiFi.localIP().toString().c_str(), UDP_PORT);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    udp_started = false;
    if (connectWifi()) {
      udp.begin(UDP_PORT);
      udp_started = true;
      Serial.printf("Ponte UDP:%u pronta. Use no PC:  nc -u %s %u\n",
                    UDP_PORT, WiFi.localIP().toString().c_str(), UDP_PORT);
    } else {
      delay(3000);
      return;
    }
  }

  if (!udp_started) {
    return;
  }

  int pkt = udp.parsePacket();
  if (pkt <= 0) {
    return;
  }

  char cmd[256];
  int len = udp.read(cmd, sizeof(cmd) - 1);
  if (len < 0) {
    len = 0;
  }
  cmd[len] = '\0';
  while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r')) {
    cmd[--len] = '\0';
  }
  if (len == 0) {
    return;
  }

  IPAddress remoteIp   = udp.remoteIP();
  uint16_t  remotePort = udp.remotePort();

  while (Serial2.available()) {
    Serial2.read();
  }
  Serial2.print(cmd);
  Serial2.print('\n');

  String resp;
  resp.reserve(512);
  unsigned long start  = millis();
  unsigned long lastRx = millis();
  while ((millis() - lastRx < RESP_IDLE_MS) && (millis() - start < RESP_MAX_MS)) {
    while (Serial2.available()) {
      resp += (char)Serial2.read();
      lastRx = millis();
    }
    delay(1);
  }
  if (resp.length() == 0) {
    resp = "(sem resposta do STM)\n";
  }

  udp.beginPacket(remoteIp, remotePort);
  udp.write((const uint8_t *)resp.c_str(), resp.length());
  udp.endPacket();

  Serial.printf("cmd \"%s\" -> %u bytes\n", cmd, (unsigned)resp.length());
}
