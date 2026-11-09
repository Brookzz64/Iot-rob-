  #include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>   // necessario para esp_wifi_set_channel()
#include <DHT.h>

// ============================================================================
// CANAL ESP-NOW (DEVE ser o MESMO no emissor e no receptor arduinoide).
// Mantenha este valor identico ao WIFI_CANAL definido no arduinoide.
// ============================================================================
#define ESPNOW_CANAL 11

// --- MAPEAMENTO DE PINOS (ESP32-S3 Dev Module) ---
// HC-SR04 #1 (Nível do Tanque)
#define TRIG1_PIN    5
#define ECHO1_PIN    6

// HC-SR04 #2 (Simulador de Presença PIR)
#define TRIG2_PIN    12
#define ECHO2_PIN    13

// Demais Sensores e Atuadores
#define DHT_PIN      4
#define DHT_TYPE     DHT22
#define LDR_PIN      1       // Entrada analógica ADC1
#define LED_VERDE    7
#define LED_VERMELHO 15

// Limite de distância para considerar "Presença Detectada" (em cm)
#define LIMITE_PRESENCA_CM 80.0 

// ============================================================================
// >>> SUBSTITUA pelos 6 bytes REAIS do MAC do ESP monitor (arduinoide) <<<
// O valor aparece no Serial do arduinoide: "Endereço MAC de Monitoramento: ..."
// Ex.: um MAC "A1:B2:C3:D4:E5:F6" vira {0xA1,0xB2,0xC3,0xD4,0xE5,0xF6}
// (0xFF x6 e BROADCAST e NAO entrega corretamente a um peer unicast)
// ============================================================================
uint8_t macMonitorizacao[] = {0x80, 0xB5, 0x4E, 0xC5, 0x8E, 0xA4};

// Estrutura de dados com a presença via Ultrassom 2
typedef struct struct_mensagem {
  float nivel_tinta;
  float temperatura;
  float humidade;
  int luminosidade;
  int presenca; // 1 = Detectada, 0 = Ausente
} struct_mensagem;

struct_mensagem dadosLeitura;
DHT dht(DHT_PIN, DHT_TYPE);
esp_now_peer_info_t peerInfo;

// Callback de confirmação de envio via ESPNOW (Compatível com ESP32 Core v3.x)
void aoEnviar(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("[ESPNOW] Status da transmissão: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("SUCESSO (Pacote entregue)");
    digitalWrite(LED_VERDE, HIGH);
    delay(100);
    digitalWrite(LED_VERDE, LOW);
  } else {
    Serial.println("FALHA no envio!");
    digitalWrite(LED_VERMELHO, HIGH);
    delay(100);
    digitalWrite(LED_VERMELHO, LOW);
  }
}


// Função auxiliar para ler distância de um sensor HC-SR04
float lerDistanciaCM(int pinTrig, int pinEcho) {
  digitalWrite(pinTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinTrig, LOW);
  
  long duracao = pulseIn(pinEcho, HIGH, 30000); // Timeout de 30ms
  if (duracao == 0) return 999.0; // Sem eco/fora de alcance
  return (duracao * 0.0343) / 2.0;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Pausa para estabilização do USB CDC no ESP32-S3
  
  Serial.println("\n==================================================");
  Serial.println("    INICIALIZANDO ESP32 - CHÃO DE FÁBRICA         ");
  Serial.println("==================================================");

  // Pinos dos dois sensores Ultrassônicos
  pinMode(TRIG1_PIN, OUTPUT);
  pinMode(ECHO1_PIN, INPUT);
  pinMode(TRIG2_PIN, OUTPUT);
  pinMode(ECHO2_PIN, INPUT);

  // Pinos dos LEDs
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  dht.begin();
  Serial.println("[OK] Sensor DHT22 inicializado.");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // garante que nao fique preso tentando conectar a um AP

  // Fixa o canal do radio ANTES de iniciar o ESP-NOW. Tem que ser igual ao
  // canal do receptor (arduinoide), senao o pacote nunca chega.
  esp_wifi_set_channel(ESPNOW_CANAL, WIFI_SECOND_CHAN_NONE);

  Serial.print("[INFO] Endereço MAC Local: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[INFO] Canal ESP-NOW fixado em: %d\n", ESPNOW_CANAL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERRO] Erro ao inicializar o ESP-NOW!");
    return;
  }
  Serial.println("[OK] ESP-NOW inicializado com sucesso.");

  esp_now_register_send_cb(aoEnviar);

  memcpy(peerInfo.peer_addr, macMonitorizacao, 6);
  peerInfo.channel = ESPNOW_CANAL;  // mesmo canal fixado acima
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERRO] Falha ao registrar o dispositivo Receptor!");
    return;
  }
  Serial.println("[OK] Dispositivo Receptor registrado.");
  Serial.println("==================================================\n");
}

void loop() {
  Serial.println("\n--- Nova Leitura dos Sensores ---");

  // 1. Ultrassom #1 (Nível do Tanque)
  // map() do Arduino e inteiro (long) e truncaria os floats; calculo em float:
  float dist_tanque = lerDistanciaCM(TRIG1_PIN, ECHO1_PIN);
  float dist_clamp = constrain(dist_tanque, 5.0f, 30.0f);
  float nivel_percentual = (30.0f - dist_clamp) / (30.0f - 5.0f) * 100.0f;

  // 2. Ultrassom #2 (Presença)
  float dist_presenca = lerDistanciaCM(TRIG2_PIN, ECHO2_PIN);
  int presenca_detectada = (dist_presenca < LIMITE_PRESENCA_CM) ? 1 : 0;

  // 3. Sensor DHT22 (Temperatura e Umidade)
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  // 4. Sensor LDR
  int luz = analogRead(LDR_PIN);

  // Guardando os dados na struct
  dadosLeitura.nivel_tinta = nivel_percentual;
  dadosLeitura.temperatura = isnan(temp) ? 0.0 : temp;
  dadosLeitura.humidade = isnan(hum) ? 0.0 : hum;
  dadosLeitura.luminosidade = luz;
  dadosLeitura.presenca = presenca_detectada;

  // Exibição completa no Monitor Serial
  Serial.printf("Tanque (HC-SR04 #1)   : %.1f cm -> Nível: %.1f%%\n", dist_tanque, dadosLeitura.nivel_tinta);
  Serial.printf("Presença (HC-SR04 #2) : %.1f cm -> %s\n", dist_presenca, dadosLeitura.presenca ? "PRESENÇA DETECTADA" : "Sem presença");
  Serial.printf("Temperatura / Umidade : %.1f ºC | %.1f %%\n", dadosLeitura.temperatura, dadosLeitura.humidade);
  Serial.printf("Luminosidade (LDR)    : %d (ADC)\n", dadosLeitura.luminosidade);

  // Alerta local de Nível de Tinta
  if (dadosLeitura.nivel_tinta < 20.0) {
    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(LED_VERDE, LOW);
    Serial.println("ESTADO: [ALERTA] Nível de tinta baixo (< 20%)!");
  } else {
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_VERMELHO, LOW);
    Serial.println("ESTADO: [OPERAÇÃO NORMAL] Nível seguro.");
  }

  // Envio via ESPNOW
  Serial.println("Enviando pacote de dados via ESPNOW...");
  esp_now_send(macMonitorizacao, (uint8_t *) &dadosLeitura, sizeof(dadosLeitura));

  delay(2000);
}



