# Projeto Final Embarcatech
## Controle de Lampada Intelbras por Raspberry Pi Pico W (BitDogLab)

## 1. Introducao
Este projeto apresenta o desenvolvimento de um sistema embarcado para controle local de uma lampada inteligente Intelbras, utilizando a plataforma BitDogLab com Raspberry Pi Pico W. O objetivo principal foi implementar uma interface fisica com joystick, botoes e display OLED para controlar liga/desliga, brilho e cor da lampada por meio do protocolo Tuya Local, sem dependencia da nuvem.

O trabalho integra conceitos de sistemas embarcados, comunicacao em rede, interfaces homem-maquina e organizacao de software em maquina de estados.

## 2. Objetivos
### 2.1 Objetivo geral
Desenvolver um controlador embarcado de lampada inteligente com resposta em tempo real e operacao local via Wi-Fi.

### 2.2 Objetivos especificos
- Conectar a Pico W a rede Wi-Fi local.
- Implementar comunicacao com a lampada via protocolo Tuya (v3.5 com fallback).
- Criar interface de uso com tres modos: liga/desliga, brilho e cor.
- Exibir informacoes de status em display OLED.
- Garantir robustez de operacao com tratamento de erro e heartbeat.

## 3. Materiais e ferramentas
### 3.1 Hardware
- Raspberry Pi Pico W (BitDogLab)
- Display OLED SSD1306 (I2C)
- Joystick analogico (ADC)
- Botoes A e B
- Lampada Intelbras compativel com Tuya Local

### 3.2 Software
- C/C++ (Pico SDK 2.2.0)
- CMake + Ninja
- Toolchain ARM (`arm-none-eabi`)
- VS Code + extensoes Pico
- Git e GitHub

## 4. Arquitetura do sistema
O software foi estruturado em modulos:
- `Projeto_Final.c`: logica principal e maquina de estados.
- `tuya.c/.h`: comunicacao com a lampada (pacotes, criptografia e comandos DPS).
- `joystick.c/.h`: leitura de direcao e valores do joystick.
- `buttons.c/.h`: eventos de botoes com debounce/long press.
- `oled.c/.h`: telas de interface no display.
- `led_matrix.c/.h`: controle da matriz WS2812 (desativada por requisito final).
- `aes.c/.h`: suporte de criptografia auxiliar.
- `config.h`: parametros de rede, GPIOs e DPS.

### 4.1 Maquina de estados
Foram implementados quatro estados:
- `CONNECTING`: conexao Wi-Fi e preparacao do sistema.
- `IDLE`: modo liga/desliga da lampada.
- `BRIGHTNESS`: ajuste de brilho pelo joystick.
- `COLOR`: selecao de cor por cursor em grade 5x5.

Essa organizacao simplifica a manutencao e evita conflitos de comando entre modos.

## 5. Implementacao
### 5.1 Conectividade e protocolo
A Pico W realiza conexao com a rede local e envia comandos para a lampada usando Tuya Local na porta 6668. O sistema realiza tentativas de comunicacao em diferentes formatos (v3.5 e fallback), com validacao de resposta e codigos de erro para diagnostico.

Tambem foi implementado heartbeat periodico para manter a sessao ativa e reduzir falhas de comunicacao.

### 5.2 Controle de lampada
Foram implementadas funcoes para:
- ligar/desligar (`DP_SWITCH`),
- ajustar brilho (`DP_BRIGHTNESS`),
- ajustar cor (`DP_COLOR` e `DP_MODE`),
- alternar para branco (`DP_MODE=white` e `DP_COLOR_TEMP`).

O sistema tambem consulta o estado inicial da lampada ao iniciar, evitando inconsistencias de status local.

### 5.3 Interface do usuario
- **Botao A**: acao principal (liga/desliga no modo IDLE, confirmacao em modos de ajuste).
- **Botao B**: troca entre modos.
- **Joystick**:
  - brilho: esquerda diminui e direita aumenta;
  - cor: move cursor na grade.
- **OLED**: exibe telas de conexao, estado atual, nivel de brilho e seletor de cor.

### 5.4 Regras finais de uso (UX)
- O brilho e confirmado por acao explicita do usuario.
- A cor escolhida e mantida entre os modos.
- O retorno para branco ocorre no ciclo OFF->ON no modo liga/desliga.
- A matriz fisica de LEDs foi mantida apagada por requisito final do projeto.

## 6. Testes e validacao
Foram realizados testes funcionais por etapas:
- conexao Wi-Fi e reconexao apos reset;
- envio de comandos de power, brilho e cor;
- verificacao de resposta da lampada e tratamento de erros;
- validacao dos eixos do joystick e navegacao dos modos;
- verificacao visual das telas OLED;
- validacao de persistencia de cor/brilho conforme regra de operacao.

Resultados observados:
- controle da lampada funcionando localmente;
- navegacao por estados estavel;
- interface OLED funcional e legivel;
- comportamento final ajustado de acordo com os requisitos definidos.

## 7. Dificuldades encontradas
Durante o desenvolvimento, os principais desafios foram:
- ajuste fino da comunicacao Tuya (formato de pacote e compatibilidade);
- tratamento de respostas invalidas/timeout;
- mapeamento correto dos eixos do joystick na placa;
- definicao de comportamento consistente entre modos (brilho x cor).

Esses pontos foram resolvidos por depuracao incremental via serial, modularizacao do codigo e revisao das regras de transicao de estados.

## 8. Conclusao
O projeto atingiu o objetivo proposto de controlar uma lampada inteligente Intelbras por meio de um sistema embarcado local com Raspberry Pi Pico W. A solucao final apresentou funcionamento estavel, interface intuitiva e boa organizacao de software, consolidando conceitos importantes de programacao embarcada, redes e integracao de perifericos.

Como trabalhos futuros, pode-se incluir:
- leitura de estado completo (modo/cor/brilho) em tempo real,
- perfis de cena personalizados,
- persistencia de configuracoes em memoria nao volatil,
- integracao com aplicativo proprio.

## 9. Referencias
- Raspberry Pi Pico SDK Documentation.
- Tuya Developer Platform (IoT) - documentacao de DPS e protocolo local.
- Datasheet SSD1306.
- Documentacao e exemplos da plataforma BitDogLab.

