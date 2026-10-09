<div align="center">

# 🌱 EstufaSmart
### Monitoramento de estufa com Arduino UNO R4 WiFi

**Temperatura · umidade · luminosidade · janela · alertas locais · telemetria Wi-Fi**

</div>

Projeto de treino para migrar o sistema EstufaSmart ao **Arduino UNO R4 WiFi**. O firmware avalia as condições localmente, sinaliza o estado nos LEDs e na matriz integrada e mantém uma fila de telemetrias para transmitir quando o servidor estiver acessível.

> A prova anexada define os requisitos e a pinagem sugerida. A frase do próprio enunciado que tenta limitar a completude do código é uma instrução destinada ao aluno durante a prova; este repositório atende ao pedido do grupo por um exemplo completo para estudo.

## O que está incluído

| Arquivo | Para que serve |
|---|---|
| `estufasmart.ino` | Firmware do Arduino: leitura, máquina de estados, alarmes, fila offline e HTTP. |
| `wifi_secrets.example.h` | Modelo de configuração da rede e calibração do LDR. |
| `servidor.py` | API HTTP, validação, banco SQLite e painel com gráfico. |

## Componentes e ligações

| Componente | Pino UNO R4 WiFi | Observação |
|---|---:|---|
| DHT11 (dados) | D2 | No código, `DHT_TYPE` está como `DHT11`. |
| Reed / tilt (janela) | D3 | Entrada com `INPUT_PULLUP`; firmware assume ativo em LOW. |
| Botão de reconhecimento | D4 | Um terminal no pino e outro no GND; ativo em LOW. |
| LDR | A0 | Use divisor de tensão; veja a seção de alimentação. |
| LED verde | D5 | Use resistor em série (tipicamente 220–330 Ω). |
| LED amarelo | D6 | Use resistor em série (tipicamente 220–330 Ω). |
| LED vermelho | D7 | Use resistor em série (tipicamente 220–330 Ω). |
| Buzzer | D8 | Ajuste `BUZZER_PASSIVO` conforme o componente. |
| Módulo relé / ventilação | D9 | Ajuste `RELE_ATIVO_EM_NIVEL_BAIXO` para o módulo. |
| Todos os módulos | GND comum | Compartilhe o GND entre placa e circuito. |

**As ligações acima seguem a pinagem recomendada no enunciado.** Os LEDs externos não são a matriz integrada; a matriz 12×8 é comandada pela biblioteca da própria placa e não consome pinos do cabeçalho.

### Atenção à alimentação de 3,3 V

Vocês informaram que o professor pediu para alimentar os módulos em **3,3 V**. A prova anexada sugere 5 V para os sensores, então há uma diferença entre a folha e a orientação de bancada que vocês receberam; este projeto segue a orientação mais recente que vocês relataram. Confiram a tensão permitida no módulo específico antes de ligar cada componente. O UNO R4 WiFi opera com lógica de 5 V, então sinais HIGH de 3,3 V podem ficar próximos do limiar de leitura de algumas entradas; além disso, algumas placas de sensor têm pull-up próprio para 5 V. Não conectem ao Arduino uma saída que possa subir acima de 5 V e não alimentem um módulo de 3,3 V pela saída 5 V.

Para o LDR, montem o divisor entre **3,3 V e GND**, com o nó central em A0 e o resistor de 10 kΩ entre o nó e um dos trilhos (a posição do LDR e do resistor muda a direção da leitura, não a segurança). Assim, A0 não recebe mais do que a alimentação do divisor. Não liguem o LDR diretamente entre alimentação e A0 sem o resistor.

O reed e o botão estão configurados com pull-up interno da placa. Se o módulo do sensor de janela trouxer uma saída eletrônica ativa em 3,3 V, validem a leitura HIGH no Serial Monitor e verifiquem que seu circuito não injeta 5 V no sinal. Se a leitura ficar instável, usem um conversor de nível adequado ou uma interface com pull-up dimensionado para o módulo. Para DHT11, buzzer e relé, confira no datasheet do módulo se aceita alimentação e nível lógico de 3,3 V; relés frequentemente exigem alimentação separada e podem não acionar diretamente pelo pino do microcontrolador.

> Ventiladores, exaustores e cargas de rede elétrica não devem ser alimentados pelo pino do Arduino. Use fonte apropriada e módulo de acionamento dimensionado para a carga; peça ao professor para conferir a ligação antes de energizar. Para este treino, é possível testar o pino D9 sem conectar uma carga real.

## Lógica de operação

O firmware verifica os sensores a cada **5 segundos**, usando `millis()` no loop. Não usa `delay()` no loop principal. LED verde indica `NORMAL`; amarelo indica `ATENCAO`; vermelho indica `CRITICO` e `RECONHECIDO`; amarelo e vermelho juntos indicam `FALHA_SENSOR`. A matriz integrada apresenta N, A, C, R ou F.

| Estado | Regra implementada | Saídas |
|---|---|---|
| `NORMAL` | Temperatura entre 20 °C e 28 °C, janela fechada, sem proximidade do limite e sem luz alta. | LED verde. |
| `ATENCAO` | Temperatura ≤21 °C ou ≥27 °C; janela aberta há pelo menos 20 s; ou luz ≥85%. | LED amarelo e bipe a cada 10 s. |
| `CRITICO` | Temperatura fora de 20–28 °C por pelo menos 30 s; ou janela aberta por pelo menos 60 s. | LED vermelho, buzzer contínuo e relé ligado. |
| `RECONHECIDO` | Botão pressionado durante `CRITICO`. | Buzzer silenciado; LED vermelho e relé permanecem ligados enquanto a condição crítica persistir. |
| `FALHA_SENSOR` | Três leituras inválidas consecutivas do DHT11. | LEDs amarelo e vermelho; telemetria informa `null` para temperatura/umidade se ainda não houve leitura válida. |

`ATENCAO` usa a margem de 1 °C como escolha explícita de implementação, pois a prova diz “perto do limite” sem definir uma margem numérica. Luz alta é parametrizada em 85%. Ajuste `TEMP_PERTO_MIN`, `TEMP_PERTO_MAX` e `LUZ_ALTA_PCT` para combinar com a demonstração do grupo. Pressionar o botão silencia o alarme crítico; o estado reconhecido só volta a normal quando temperatura, janela, proximidade e luz normalizarem.

## Preparação do Arduino IDE

1. Instale o pacote de placas **Arduino UNO R4 Boards** e selecione **Arduino UNO R4 WiFi**.
2. Instale as bibliotecas **DHT sensor library** (Adafruit) e **ArduinoHttpClient** pelo Library Manager. A biblioteca da matriz e `WiFiS3` vêm com o pacote da placa; `ArduinoGraphics` pode ser solicitada pelo gerenciador se o IDE indicar dependência ausente.
3. Copie `wifi_secrets.example.h` para `wifi_secrets.h` na mesma pasta do sketch.
4. Preencha `WIFI_SSID`, `WIFI_PASSWORD`, `DEVICE_ID`, `SERVER_HOST` e `SERVER_PORT` no arquivo privado. `SERVER_HOST` é o IP do computador na rede local; **não** use `localhost` ou `127.0.0.1` no Arduino.
5. Calibre `LDR_ADC_ESCURO` e `LDR_ADC_CLARO`: abra o Serial Monitor a 115200 baud, observe `analogRead(A0)` no escuro e sob luz forte, e copie valores que representem esses extremos.
6. Compile e envie o sketch. Abra o Serial Monitor a **115200 baud** para acompanhar estado, leituras e tamanho da fila.

No computador, descubra o IP local (por exemplo, `ipconfig` no Windows) e use esse IP em `SERVER_HOST`. O computador e a placa precisam estar na mesma rede Wi-Fi. Se o sistema operacional bloquear conexões, permita a porta TCP 8000 na rede privada. Evite expor o servidor diretamente à internet.

## Rodar API, banco e painel

O servidor usa apenas a biblioteca padrão do Python 3. Não precisa instalar Flask ou FastAPI.

```bash
python servidor.py
```

Abra `http://localhost:8000` no computador para ver o painel. O Arduino envia para `http://IP_DO_COMPUTADOR:8000/api/telemetria`. O endpoint `GET /api/leituras` retorna as últimas 100 leituras em JSON. O arquivo `estufasmart.sqlite3` é criado automaticamente no diretório de execução.

Opcionalmente, configure `ESTUFA_HOST`, `ESTUFA_PORT` e `ESTUFA_DB` antes de iniciar o servidor. Para receber conexões da rede local, mantenha o host padrão `0.0.0.0`; o painel então fica acessível a outros dispositivos dessa rede.

## Fila offline e rede

Cada amostra é adicionada à fila local do firmware e uma leitura pendente é tentada por passagem do loop. Em caso de falha, permanece na fila; quando cheia, a fila circular conserva as 20 amostras mais recentes. **A fila está na RAM**: sobrevive à queda do Wi-Fi, mas não a uma reinicialização ou falta de energia. O estado e os alarmes locais continuam funcionando sem Wi-Fi. HTTP não cifra os dados; use apenas em uma rede de laboratório confiável. Para implantação real, migre para HTTPS com autenticação.

## Roteiro de demonstração

1. Inicie `servidor.py`, ligue a placa e confirme `NORMAL` nos LEDs/matriz e no painel.
2. Aqueça o DHT11 suavemente (mãos próximas ou ar morno, sem chama nem calor excessivo). Ao sair da faixa ideal, mantenha a condição por mais de 30 s para observar `CRITICO` e o relé.
3. Pressione o botão no estado crítico: o buzzer deve silenciar, mas o LED vermelho e a ventilação ficam ativos enquanto o ambiente continuar crítico.
4. Deixe a leitura normalizar para voltar a `NORMAL`. Teste separadamente janela e LDR; não dependa deles para demonstrar superaquecimento.
5. Para demonstrar a fila, interrompa o Wi-Fi/servidor por alguns instantes e confira o contador `fila` no Serial Monitor; restabeleça a conexão e observe o envio pendente.

## Limites conhecidos e próximos passos

- Reiniciar a placa apaga as amostras pendentes da fila RAM. O contador `seq` usa um contador de inicializações na EEPROM para não colidir com leituras anteriores no SQLite.
- O exemplo usa HTTP sem TLS para manter o exercício local simples. Não envie credenciais ou dados sensíveis por rede não confiável.
- A calibração de luminosidade depende da montagem do divisor e do ambiente; 85% é um limiar de treino, não uma medida física em lux.
- O firmware supõe reed fechado em HIGH, botão pressionado em LOW, buzzer ativo (mude a constante se passivo) e módulo de relé ativo em LOW (confira o jumper/datasheet).
- O servidor armazena leituras duplicadas de forma idempotente por `device_id` + `seq` e rejeita valores fora das faixas.

## Referência do exercício

Requisitos, estados e pinagem deste exemplo foram adaptados da **SENAI Dev Experience — Módulo B — Prova de Treino**, Parte 1, páginas 1–3. A referência de 5 V e a documentação dos pinos vêm do [guia oficial de pinos do UNO R4 WiFi](https://docs.arduino.cc/tutorials/uno-r4-wifi/cheat-sheet/). A matriz nativa é controlada pela biblioteca Arduino LED Matrix incluída no pacote da placa. O código não substitui a revisão da fiação nem os datasheets dos módulos que vocês têm em mãos.
