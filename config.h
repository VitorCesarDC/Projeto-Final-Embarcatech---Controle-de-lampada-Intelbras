#ifndef CONFIG_H
#define CONFIG_H
#define LAMP_PORT 6668
/* ==========================================================
 *  config.h — Configurações do usuário
 *
 *  EDITE AQUI antes de compilar:
 *    1. WIFI_SSID / WIFI_PASSWORD
 *    2. TUYA_DEVICE_ID / TUYA_LOCAL_KEY  (obtidos em iot.tuya.com)
 *    3. LAMP_IP  (IP local da lâmpada na sua rede)
 * ========================================================== */

/* ── Wi-Fi ─────────────────────────────────────────────── */
#define WIFI_SSID       "ssid_wifi_aqui"
#define WIFI_PASSWORD   "senha_wifi_aqui"

/* ── Tuya Local Protocol ───────────────────────────────── */
#define TUYA_DEVICE_ID  "device_id_aqui"   /* 20 chars */
#define TUYA_LOCAL_KEY  "local_key_aqui"        /* 16 chars */
#define LAMP_IP         "Ip_da_lampada_aqui"           /* Ex: "
#define TUYA_PORT       6668

/* ── GPIOs (BitDogLab padrão) ──────────────────────────── */
#define BTN_A_PIN        5    /* Botão A — liga/desliga */
#define BTN_B_PIN        6    /* Botão B — troca modo   */

#define LED_MATRIX_PIN   7    /* WS2812B 5×5            */

#define OLED_I2C_PORT    i2c1
#define OLED_SDA_PIN    14
#define OLED_SCL_PIN    15
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT      64

#define JOY_X_PIN       26    /* ADC0 — eixo horizontal */
#define JOY_Y_PIN       27    /* ADC1 — eixo vertical   */

/* ── Matriz de LEDs ────────────────────────────────────── */
#define MATRIX_COLS      5
#define MATRIX_ROWS      5
#define MATRIX_LEDS     25    /* 5 × 5                  */
#define MATRIX_BRIGHT   40    /* 0-255: brilho padrão   */

/* ── Temporização ──────────────────────────────────────── */
#define DEBOUNCE_MS      50
#define LONG_PRESS_MS  1000
#define HEARTBEAT_MS  10000
#define JOY_UPDATE_MS   120   /* intervalo de leitura do joystick */
#define JOY_DEADZONE    300   /* zona morta ADC (0-4095) */

/* ── Tuya DPS da lâmpada Intelbras EWS 410 ─────────────── */
/*   Confirmados via API Tuya (category: dj)                */
#define DP_SWITCH       20    /* Boolean – switch_led          */
#define DP_MODE         21    /* Enum    – work_mode           */
                              /*   "white"|"colour"|"scene"|"music" */
#define DP_BRIGHTNESS   22    /* Integer – bright_value_v2 (10-1000) */
#define DP_COLOR_TEMP   23    /* Integer – temp_value_v2   (0-1000)  */
#define DP_COLOR        24   /* Json    – colour_data_v2            */
                              /*   {"h":0-360,"s":0-1000,"v":0-1000} */

#endif /* CONFIG_H */