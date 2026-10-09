#pragma once
// Copie para wifi_secrets.h (ignorado pelo Git) e preencha para sua rede.
#define WIFI_SSID "NOME_DA_REDE"
#define WIFI_PASSWORD "SENHA_DA_REDE"
#define DEVICE_ID "estufa-sala01"

// IP do computador/servidor na mesma rede do Arduino. Não use localhost aqui.
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 8000
#define SERVER_PATH "/api/telemetria"

// Calibre olhando os valores no Serial Monitor: escuro e claro são contagens ADC.
#define LDR_ADC_ESCURO 100
#define LDR_ADC_CLARO 900
