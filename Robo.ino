#include <Arduino.h>

// --- Definicao dos Pinos ---
const int PIN_JOY_X = 1; 
const int PIN_JOY_Y = 2; 

const int PIN_BTN_DESLIGAR = 4;

const int PIN_LED_VERDE = 5;
const int PIN_LED_VERMELHO = 6;

// --- Configurações do Joystick ---
const int ADC_CENTRO = 2048;
const int MARGEM_MORTA = 600;

// --- Variaveis de Estado ---
bool roboLigado = true;
bool estadoBotaoAnterior = HIGH; // Para detectar a borda de descida (clique)
unsigned long ultimoTempoDebounce = 0;
const unsigned long TEMPO_DEBOUNCE = 50;

String ultimoComando = "";

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERMELHO, OUTPUT);

  pinMode(PIN_BTN_DESLIGAR, INPUT_PULLUP);

  digitalWrite(PIN_LED_VERDE, HIGH);
  digitalWrite(PIN_LED_VERMELHO, LOW);

  Serial.println("--- Controle Remoto ESP32-S3 Iniciado ---");
  Serial.println("Status Inicial: ROBO LIGADO (LED Verde)");
}

void loop() {
  // --- LEITURA E TRATAMENTO DO BOTÃO DE DESLIGAR/LIGAR ---
  int leituraBotao = digitalRead(PIN_BTN_DESLIGAR);

  // Se o botão mudou de estado, reseta o tempo de debounce
  if (leituraBotao != estadoBotaoAnterior) {
    ultimoTempoDebounce = millis();
  }

  if ((millis() - ultimoTempoDebounce) > TEMPO_DEBOUNCE) {
    // Detecta o clique único (transição HIGH -> LOW)
    static bool estadoProcessado = HIGH;
    
    if (leituraBotao == LOW && estadoProcessado == HIGH) {
      roboLigado = !roboLigado; // Inverte o estado do robô

      if (roboLigado) {
        digitalWrite(PIN_LED_VERDE, HIGH);
        digitalWrite(PIN_LED_VERMELHO, LOW);

        Serial.println("\n------------------------------------");
        Serial.println("Comando enviado: LIGAR");
        Serial.println("Status: ROBO LIGADO (LED Verde)");
        Serial.println("------------------------------------");
      } else {
        digitalWrite(PIN_LED_VERDE, LOW);
        digitalWrite(PIN_LED_VERMELHO, HIGH);

        Serial.println("\n------------------------------------");
        Serial.println("Comando enviado: DESLIGAR");
        Serial.println("Status: ROBO DESLIGADO (LED Vermelho)");
        Serial.println("------------------------------------");
      }
    }
    estadoProcessado = leituraBotao;
  }
  estadoBotaoAnterior = leituraBotao;

  // --- LEITURA DO JOYSTICK E ENVIO DE COMANDOS ---
  if (roboLigado) {
    int valorX = analogRead(PIN_JOY_X);
    int valorY = analogRead(PIN_JOY_Y);

    String comandoAtual = "PARADO";

    if (valorY > (ADC_CENTRO + MARGEM_MORTA)) {
      comandoAtual = "Frente";
    } else if (valorY < (ADC_CENTRO - MARGEM_MORTA)) {
      comandoAtual = "Trás";
    }
    else if (valorX < (ADC_CENTRO - MARGEM_MORTA)) {
      comandoAtual = "Esquerda";
    } else if (valorX > (ADC_CENTRO + MARGEM_MORTA)) {
      comandoAtual = "Direita";
    }

    if (comandoAtual != ultimoComando) {
      Serial.print("Comando: ");
      Serial.println(comandoAtual);
      ultimoComando = comandoAtual;
    }
  }

  delay(100);
}
