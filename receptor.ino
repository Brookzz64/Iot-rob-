#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>   // necessario para esp_wifi_set_channel()
#include <HTTPClient.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <math.h>       // isnan() / isinf()

// Hardware Matriz MAX7219 no ESP32-S3
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   1
#define DATA_PIN      11
#define CS_PIN        10
#define CLK_PIN       12

MD_Parola Display = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

#define LED_VERDE    7
#define LED_VERMELHO 15

// ============================================================================
// >>> PREENCHA ESTES 3 VALORES COM OS DADOS REAIS <<<
// ============================================================================
const char* ssid     = "rede";    // nome da sua rede Wi-Fi
const char* password = "senha";   // senha da sua rede Wi-Fi
// IP da maquina que roda o backend Python (uvicorn em host=0.0.0.0).
// Descubra com "ipconfig" (Windows) no PC do servidor.
const char* serverUrl = "http://192.168.1.100:8000/api/telemetria";

// ATENCAO (ESP-NOW + Wi-Fi): o radio e unico e ESP-NOW usa o MESMO canal do Wi-Fi.
// Este receptor conecta ao roteador e, por isso, fica no CANAL DO ROTEADOR.
// O emissor (espfabrica) precisa transmitir NESSE MESMO canal.
//
// PASSO OBRIGATORIO DE CONFIGURACAO:
//   1. Faca o upload deste firmware e abra o Serial.
//   2. Anote o numero em "[INFO] Canal do radio fixado em: X".
//   3. No espfabrica, defina #define ESPNOW_CANAL X com esse mesmo numero.
//   4. Anote tambem o "Endereço MAC de Monitoramento" e coloque no espfabrica.

// Estrutura de dados recebida via ESPNOW
typedef struct struct_mensagem {
  float nivel_tinta;
  float temperatura;
  float humidade;
  int luminosidade;
  int presenca;
} struct_mensagem;

struct_mensagem dadosTemp = {0};      // Preenchido na callback
struct_mensagem dadosRecebidos = {0}; // Copiado com segurança para uso no loop
bool recebeuAlgumDado = false;        // false ate o primeiro pacote ESP-NOW chegar

volatile bool novosDados = false;
unsigned long ultimaRecepcao = 0;
int ecraAtual = 0;
unsigned long ultimoChaveamentoTela = 0;

// Callback chamado ao receber o pacote ESPNOW (mantenha leve)
void aoReceber(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_mensagem)) {
    memcpy(&dadosTemp, incomingData, sizeof(dadosTemp));
    novosDados = true;
    ultimaRecepcao = millis();
  }
}

// Converte float para texto JSON seguro: nan/inf viram 0 (JSON nao aceita nan/inf
// e a API Python rejeitaria o pacote com erro 422).
String floatJsonSeguro(float v) {
  if (isnan(v) || isinf(v)) {
    return String("0.0");
  }
  return String(v, 2);  // 2 casas decimais
}

void enviarParaBackend() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"nivel_tinta\":" + floatJsonSeguro(dadosRecebidos.nivel_tinta) + ",";
    json += "\"temperatura\":" + floatJsonSeguro(dadosRecebidos.temperatura) + ",";
    json += "\"humidade\":" + floatJsonSeguro(dadosRecebidos.humidade) + ",";
    json += "\"luminosidade\":" + String(dadosRecebidos.luminosidade) + ",";
    json += "\"presenca\":" + String(dadosRecebidos.presenca);
    json += "}";

    Serial.println("[HTTP] Enviando JSON ao Backend Python...");
    int httpCode = http.POST(json);

    if (httpCode > 0) {
      Serial.printf("[HTTP] Resposta do Servidor (Código %d)\n", httpCode);
    } else {
      Serial.printf("[HTTP ERRO] Falha no POST: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println("[HTTP WARNING] Wi-Fi desconectado!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================================");
  Serial.println("    INICIALIZANDO ESP32 - MONITORAMENTO           ");
  Serial.println("==================================================");

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  Display.begin();
  Display.setIntensity(4);
  Display.displayClear();
  Display.print("INIC...");
  Serial.println("[OK] Matriz MAX7219 pronta.");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Conectando ao Wi-Fi");

  // Aguarda até 10 segundos pela conexão Wi-Fi
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[OK] Wi-Fi Conectado no Canal %d | IP: %s\n", WiFi.channel(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[AVISO] Falha ao conectar no Wi-Fi. ESP-NOW operará de forma independente.");
  }

  // --------------------------------------------------------------------------
  // SINCRONIZACAO DE CANAL (correcao critica)
  // ESP-NOW e Wi-Fi compartilham o mesmo radio/canal. Se o canal do receptor
  // diferir do canal do EMISSOR ESP-NOW, o callback aoReceber() NUNCA dispara
  // e, portanto, nenhum POST e enviado -> banco fica vazio.
  // Fixamos explicitamente o canal do radio e o informamos no Serial para que
  // o emissor use EXATAMENTE o mesmo valor.
  // --------------------------------------------------------------------------
  uint8_t canal = (WiFi.status() == WL_CONNECTED) ? WiFi.channel() : 1;
  esp_wifi_set_channel(canal, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[INFO] Canal do radio fixado em: %d (configure o EMISSOR ESP-NOW neste canal)\n", canal);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERRO] Falha no ESP-NOW!");
    return;
  }
  Serial.println("[OK] ESP-NOW pronto.");

  esp_now_register_recv_cb(aoReceber);
  Serial.print("[INFO] Endereço MAC de Monitoramento: ");
  Serial.println(WiFi.macAddress());
  Serial.println("==================================================\n");
}

void loop() {
  // Processamento de novos dados com proteção contra Data Tearing
  if (novosDados) {
    noInterrupts();
    memcpy(&dadosRecebidos, &dadosTemp, sizeof(dadosRecebidos));
    novosDados = false;
    interrupts();

    recebeuAlgumDado = true;

    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_VERMELHO, LOW);

    Serial.println("\n==================================================");
    Serial.println("[ESPNOW RX] >>> PACOTE RECEBIDO DO CHÃO DE FÁBRICA <<<");
    Serial.printf("  -> Nível Tinta : %.1f %%\n", dadosRecebidos.nivel_tinta);
    Serial.printf("  -> Temperatura : %.1f ºC\n", dadosRecebidos.temperatura);
    Serial.printf("  -> Umidade     : %.1f %%\n", dadosRecebidos.humidade);
    Serial.printf("  -> Luminosidade: %d\n", dadosRecebidos.luminosidade);
    Serial.printf("  -> Presença    : %s\n", dadosRecebidos.presenca ? "ON" : "OFF");
    Serial.println("==================================================");

    enviarParaBackend();
  }

  // Enquanto nenhum pacote chegou, mostra estado de espera (evita exibir zeros).
  if (!recebeuAlgumDado) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
    Display.print("AGUARD");
  }
  // Verificação de Timeout (> 5s sem recepção)
  else if (millis() - ultimaRecepcao > 5000) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
    Display.print("S/ DADO");
  } else {
    // Alternância de telas a cada 2 segundos
    if (millis() - ultimoChaveamentoTela >= 2000) {
      ultimoChaveamentoTela = millis();

      char textoEcra[16];
      switch (ecraAtual) {
        case 0:
          snprintf(textoEcra, sizeof(textoEcra), "NVL %.0f%%", dadosRecebidos.nivel_tinta);
          break;
        case 1:
          snprintf(textoEcra, sizeof(textoEcra), "TMP %.0fC", dadosRecebidos.temperatura);
          break;
        case 2:
          snprintf(textoEcra, sizeof(textoEcra), "UMD %.0f%%", dadosRecebidos.humidade);
          break;
        case 3:
          snprintf(textoEcra, sizeof(textoEcra), "LUX %d", dadosRecebidos.luminosidade);
          break;
        case 4:
          snprintf(textoEcra, sizeof(textoEcra), "PRS %s", dadosRecebidos.presenca ? "ON" : "OFF");
          break;
      }

      Display.print(textoEcra);
      ecraAtual = (ecraAtual + 1) % 5;
    }
  }
}
