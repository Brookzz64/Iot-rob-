este serviço é um sensor que automatiza o processo de tanques em fábricas, funciona com 3 códigos:

main: Este é o principal, um ESP 32 S3 que deve ficar adjascente ao tanque, ligado com:
        1 sensor ultrassonico que mede o nivel do tanque
        1 sensor de presença, para garantir que não a ninguem próximo a maquina
        1 sensor dht, para medir humidade e temperatura
        1 fotorresistor para medir a luminosidade do local
        2 leds, um vermleho e um verde, para simbolizar coisas como transmissão de informação etc..
      Ele coleta os dados dos sensores e envia para o segundo ESP32 S3, por meio de comunicação ESPNOW

receptor: este código tem apenas um ESP 32 S3, ligado a uma placa de led. A função principal dele é em software, ele receb os dados do main, e transmite por 
          serial monitor, alem disso, ele envia os dados para um sistema de backend, para armazená-los.

banco: por ultimo, este código em python, recebe os dados por wi-fi e armazena em um arquivo separado, para monitoramento.
