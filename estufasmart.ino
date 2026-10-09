/* EstufaSmart - Arduino UNO R4 WiFi
 * Rede e firmware independentes: estados e alarmes continuam locais sem Wi-Fi.
 */
#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <DHT.h>
#include <EEPROM.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include "wifi_secrets.h"

#define DHT_TYPE DHT11
constexpr uint8_t PIN_DHT = 2, PIN_JANELA = 3, PIN_BOTAO = 4;
constexpr uint8_t PIN_LED_VERDE = 5, PIN_LED_AMARELO = 6, PIN_LED_VERMELHO = 7;
constexpr uint8_t PIN_BUZZER = 8, PIN_RELE = 9, PIN_LDR = A0;
constexpr bool RELE_ATIVO_EM_NIVEL_BAIXO = true;
constexpr bool BUZZER_PASSIVO = false; // true para buzzer passivo; false para ativo
constexpr bool JANELA_ABERTA_EM_NIVEL_BAIXO = true; // reed com INPUT_PULLUP
constexpr bool BOTAO_PRESSIONADO_EM_NIVEL_BAIXO = true;
constexpr uint32_t INTERVALO_LEITURA = 5000, DEBOUNCE_MS = 35;
constexpr float TEMP_MIN = 20.0, TEMP_MAX = 28.0;
constexpr float TEMP_PERTO_MIN = 21.0, TEMP_PERTO_MAX = 27.0;
constexpr uint8_t LUZ_ALTA_PCT = 85;
constexpr uint32_t JANELA_ATENCAO_MS = 20000, CRITICO_MS = 30000, JANELA_CRITICA_MS = 60000;
constexpr size_t FILA_MAX = 20, JSON_MAX = 256;

DHT dht(PIN_DHT, DHT_TYPE);
ArduinoLEDMatrix matriz;
WiFiClient wifi;
HttpClient http(wifi, SERVER_HOST, SERVER_PORT);

enum Estado { NORMAL, ATENCAO, CRITICO, RECONHECIDO, FALHA_SENSOR };
const char *nomeEstado[] = {"NORMAL", "ATENCAO", "CRITICO", "RECONHECIDO", "FALHA_SENSOR"};
Estado estado = NORMAL;
struct Registro { char json[JSON_MAX]; } fila[FILA_MAX];
size_t inicioFila = 0, tamanhoFila = 0;
uint32_t seq = 0, ultimaLeitura = 0, foraFaixaDesde = 0, janelaDesde = 0;
uint32_t ultimoBipe = 0, iniciouWiFi = 0, ultimaTentativaWiFi = 0, ultimaTentativaEnvio = 0;
int ultimoStatusWiFi = -1;
uint8_t falhasDht = 0;
float temperatura = NAN, umidade = NAN;
int luzPct = 0;
bool janelaAberta = false, reconhecido = false;
bool botaoEstavel = false, botaoAmostra = false;
uint32_t botaoMudouEm = 0;
constexpr uint32_t EEPROM_SEQ_MAGIC = 0x45534631UL;
constexpr uint32_t SEQ_POR_INICIALIZACAO = 1000000UL;
constexpr uint32_t MAX_INICIALIZACOES_SEQ = 2147UL;

void iniciarSequenciaPersistente() {
  uint32_t magic = 0, boot = 0;
  EEPROM.get(0, magic);
  EEPROM.get(sizeof(magic), boot);
  if (magic != EEPROM_SEQ_MAGIC || boot >= MAX_INICIALIZACOES_SEQ) boot = 0;
  ++boot;
  EEPROM.put(0, EEPROM_SEQ_MAGIC);
  EEPROM.put(sizeof(magic), boot);
  seq = boot * SEQ_POR_INICIALIZACAO;
}

bool ativo(bool raw, bool ativoBaixo) { return ativoBaixo ? !raw : raw; }
void saidaRele(bool ligado) {
  bool nivel = ligado ? !RELE_ATIVO_EM_NIVEL_BAIXO : RELE_ATIVO_EM_NIVEL_BAIXO;
  digitalWrite(PIN_RELE, nivel ? HIGH : LOW);
}
void buzzer(bool ligado) {
  if (!ligado) { noTone(PIN_BUZZER); digitalWrite(PIN_BUZZER, LOW); return; }
  if (BUZZER_PASSIVO) tone(PIN_BUZZER, 2200); else digitalWrite(PIN_BUZZER, HIGH);
}
void mostrarEstado(Estado e) {
  matriz.beginText(0, 0, 0xFFFFFF); matriz.print(nomeEstado[e][0]); matriz.endText();
}
void aplicarSaidas(uint32_t agora) {
  digitalWrite(PIN_LED_VERDE, estado == NORMAL ? HIGH : LOW);
  digitalWrite(PIN_LED_AMARELO, (estado == ATENCAO || estado == FALHA_SENSOR) ? HIGH : LOW);
  digitalWrite(PIN_LED_VERMELHO, (estado == CRITICO || estado == FALHA_SENSOR || estado == RECONHECIDO) ? HIGH : LOW);
  saidaRele(estado == CRITICO || estado == RECONHECIDO);
  bool tocar = estado == CRITICO || (estado == ATENCAO && agora - ultimoBipe >= 10000);
  if (estado == ATENCAO && tocar) ultimoBipe = agora;
  buzzer(tocar);
  mostrarEstado(estado);
}

void atualizarBotao(uint32_t agora) {
  bool amostra = ativo(digitalRead(PIN_BOTAO), BOTAO_PRESSIONADO_EM_NIVEL_BAIXO);
  if (amostra != botaoAmostra) { botaoAmostra = amostra; botaoMudouEm = agora; }
  if (agora - botaoMudouEm >= DEBOUNCE_MS && botaoEstavel != botaoAmostra) {
    botaoEstavel = botaoAmostra;
    if (botaoEstavel && estado == CRITICO) reconhecido = true;
  }
}

void atualizarEstado(uint32_t agora, bool dhtValido) {
  if (!dhtValido) { estado = falhasDht >= 3 ? FALHA_SENSOR : estado; return; }
  janelaAberta = ativo(digitalRead(PIN_JANELA), JANELA_ABERTA_EM_NIVEL_BAIXO);
  if (janelaAberta) { if (janelaDesde == 0) janelaDesde = agora; }
  else janelaDesde = 0;
  bool fora = temperatura < TEMP_MIN || temperatura > TEMP_MAX;
  if (fora) { if (foraFaixaDesde == 0) foraFaixaDesde = agora; }
  else foraFaixaDesde = 0;
  bool janelaCritica = janelaAberta && janelaDesde && agora - janelaDesde >= JANELA_CRITICA_MS;
  bool calorCritico = fora && foraFaixaDesde && agora - foraFaixaDesde >= CRITICO_MS;
  bool janelaAtencao = janelaAberta && janelaDesde && agora - janelaDesde >= JANELA_ATENCAO_MS;
  bool perto = temperatura <= TEMP_PERTO_MIN || temperatura >= TEMP_PERTO_MAX;
  bool atencao = perto || janelaAtencao || luzPct >= LUZ_ALTA_PCT;
  if (reconhecido) {
    if (!fora && !janelaAberta && !perto && luzPct < LUZ_ALTA_PCT) { reconhecido = false; estado = NORMAL; }
    else estado = RECONHECIDO;
  } else if (janelaCritica || calorCritico) estado = CRITICO;
  else if (atencao) estado = ATENCAO;
  else estado = NORMAL;
}

void enfileirar(const char *json) {
  if (tamanhoFila == FILA_MAX) { inicioFila = (inicioFila + 1) % FILA_MAX; --tamanhoFila; }
  size_t idx = (inicioFila + tamanhoFila) % FILA_MAX;
  strncpy(fila[idx].json, json, JSON_MAX - 1); fila[idx].json[JSON_MAX - 1] = '\0'; ++tamanhoFila;
}

void conectarWiFi(uint32_t agora) {
  int status = WiFi.status();
  if (status != ultimoStatusWiFi) {
    ultimoStatusWiFi = status;
    Serial.print("Wi-Fi status: "); Serial.println(status);
    if (status == WL_CONNECTED) {
      Serial.print("Wi-Fi conectado; IP do Arduino: "); Serial.println(WiFi.localIP());
      Serial.print("Servidor configurado: http://"); Serial.print(SERVER_HOST);
      Serial.print(':'); Serial.print(SERVER_PORT); Serial.println(SERVER_PATH);
    }
  }
  if (status == WL_CONNECTED) return;
  if (iniciouWiFi == 0) { WiFi.begin(WIFI_SSID, WIFI_PASSWORD); iniciouWiFi = agora; ultimaTentativaWiFi = agora; return; }
  if (agora - ultimaTentativaWiFi >= 15000) {
    ultimaTentativaWiFi = agora;
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

bool enviarUmRegistro() {
  if (WiFi.status() != WL_CONNECTED || tamanhoFila == 0) return false;
  wifi.setTimeout(1200);
  http.setTimeout(1200);
  size_t idx = inicioFila;
  int inicioPost = http.post(SERVER_PATH, "application/json", fila[idx].json);
  Serial.print("HTTP inicio POST: "); Serial.println(inicioPost);
  if (inicioPost != HTTP_SUCCESS) { http.stop(); return false; }
  int status = http.responseStatusCode();
  http.responseBody();
  http.stop();
  Serial.print("HTTP resposta do servidor: "); Serial.println(status);
  if (status >= 200 && status < 300) { inicioFila = (inicioFila + 1) % FILA_MAX; --tamanhoFila; return true; }
  return false;
}

void criarTelemetria() {
  char json[JSON_MAX];
  char tempJson[20], umidJson[20];
  if (isnan(temperatura)) strcpy(tempJson, "null"); else dtostrf(temperatura, 0, 1, tempJson);
  if (isnan(umidade)) strcpy(umidJson, "null"); else dtostrf(umidade, 0, 1, umidJson);
  snprintf(json, sizeof(json), "{\"device_id\":\"%s\",\"placa\":\"Arduino UNO R4 WiFi\",\"seq\":%lu,\"temp\":%s,\"umid\":%s,\"janela\":\"%s\",\"luz_pct\":%d,\"estado\":\"%s\"}",
    DEVICE_ID, (unsigned long)seq++, tempJson, umidJson,
    janelaAberta ? "aberta" : "fechada", luzPct, nomeEstado[estado]);
  enfileirar(json);
}

void lerSensores(uint32_t agora) {
  float t = dht.readTemperature(), h = dht.readHumidity();
  bool valido = !isnan(t) && !isnan(h);
  if (valido) { temperatura = t; umidade = h; falhasDht = 0; }
  else if (falhasDht < 255) ++falhasDht;
  int ldr = analogRead(PIN_LDR);
  luzPct = constrain(map(ldr, LDR_ADC_ESCURO, LDR_ADC_CLARO, 0, 100), 0, 100);
  janelaAberta = ativo(digitalRead(PIN_JANELA), JANELA_ABERTA_EM_NIVEL_BAIXO);
  atualizarEstado(agora, valido);
  aplicarSaidas(agora);
  criarTelemetria();
  Serial.print("Estado: "); Serial.print(nomeEstado[estado]); Serial.print(" | T: "); Serial.print(temperatura);
  Serial.print(" | RH: "); Serial.print(umidade); Serial.print(" | Luz: "); Serial.print(luzPct);
  Serial.print("% | seq: "); Serial.print(seq - 1);
  Serial.print(" | Wi-Fi: "); Serial.print(WiFi.status());
  Serial.print(" | fila: "); Serial.println(tamanhoFila);
}

void setup() {
  Serial.begin(115200);
  iniciarSequenciaPersistente();
  pinMode(PIN_LED_VERDE, OUTPUT); pinMode(PIN_LED_AMARELO, OUTPUT); pinMode(PIN_LED_VERMELHO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT); pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_JANELA, INPUT_PULLUP); pinMode(PIN_BOTAO, INPUT_PULLUP);
  saidaRele(false); buzzer(false); dht.begin(); matriz.begin(); mostrarEstado(NORMAL);
  analogReadResolution(10);
}

void loop() {
  uint32_t agora = millis();
  atualizarBotao(agora);
  if (agora - ultimaLeitura >= INTERVALO_LEITURA) {
    ultimaLeitura = agora;
    lerSensores(agora);
  }
  conectarWiFi(agora);
  if (agora - ultimaTentativaEnvio >= INTERVALO_LEITURA) {
    ultimaTentativaEnvio = agora;
    enviarUmRegistro(); // no máximo uma tentativa por amostra; alarmes locais continuam independentes
  }
}
