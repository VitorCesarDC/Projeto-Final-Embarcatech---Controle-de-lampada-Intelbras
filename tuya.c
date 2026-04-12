#include "tuya.h"
#include "aes.h"
#include "config.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include <stdio.h>
#include <string.h>

#define TUYA_PREFIX_55AA    0x000055AAu
#define TUYA_SUFFIX_55AA    0x0000AA55u
#define TUYA_PREFIX_6699    0x00006699u
#define TUYA_SUFFIX_6699    0x00009966u

#define TUYA_VER_HDR_33     "3.3\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
#define TUYA_VER_HDR_35     "3.5\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"

#define CMD_SESS_KEY_START  3
#define CMD_SESS_KEY_RESP   4
#define CMD_SESS_KEY_FINISH 5
#define CMD_HEARTBEAT       9
#define CMD_STATUS          10
#define CMD_SET             7
#define CMD_SET_ALT         13

#define TIMEOUT_MS          3000
#define WAIT_STEP_MS        10
#define MAX_PKT             768
#define MAX_RX              768

static uint32_t seq_no = 1;

typedef struct {
    struct tcp_pcb *pcb;
    bool connected;
    bool done;
    err_t err;
    uint8_t rx_buf[MAX_RX];
    uint16_t rx_len;
} tuya_conn_t;

typedef struct {
    uint32_t cmd;
    const uint8_t *payload;
    uint16_t payload_len;
} tuya_pkt_decoded_t;

static uint32_t fake_timestamp(void) {
    return (uint32_t)(to_ms_since_boot(get_absolute_time()) / 1000) + 1700000000u;
}

static void put_u16be(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)((v >> 8) & 0xFFu);
    buf[1] = (uint8_t)(v & 0xFFu);
}

static void put_u32be(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)((v >> 24) & 0xFFu);
    buf[1] = (uint8_t)((v >> 16) & 0xFFu);
    buf[2] = (uint8_t)((v >> 8) & 0xFFu);
    buf[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16be(const uint8_t *buf) {
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

static uint32_t get_u32be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           (uint32_t)buf[3];
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static bool hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return false;
    return mbedtls_md_hmac(md, key, key_len, data, data_len, out) == 0;
}

static bool aes_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *cipher, uint8_t tag[16]) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                       plain_len, iv, 12,
                                       aad, aad_len, plain, cipher, 16, tag);
    }
    mbedtls_gcm_free(&gcm);
    return rc == 0;
}

static bool aes_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *cipher, size_t cipher_len,
                            const uint8_t tag[16], uint8_t *plain) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, cipher_len, iv, 12, aad, aad_len,
                                      tag, 16, cipher, plain);
    }
    mbedtls_gcm_free(&gcm);
    return rc == 0;
}

static uint16_t packet_total_len_55aa(const uint8_t *pkt, uint16_t len) {
    if (len < 16) return 0;
    if (get_u32be(pkt) != TUYA_PREFIX_55AA) return 0;
    uint32_t payload_plus_tail = get_u32be(pkt + 12);
    uint32_t total = 16u + payload_plus_tail;
    if (total > MAX_RX) return 0;
    if (len < total) return 0;
    return (uint16_t)total;
}

static uint16_t packet_total_len_6699(const uint8_t *pkt, uint16_t len) {
    if (len < 18) return 0;
    if (get_u32be(pkt) != TUYA_PREFIX_6699) return 0;
    uint32_t l = get_u32be(pkt + 14);
    uint32_t total = 18u + l + 4u;
    if (total > MAX_RX) return 0;
    if (len < total) return 0;
    return (uint16_t)total;
}

static uint16_t packet_total_len(const uint8_t *pkt, uint16_t len) {
    uint16_t n = packet_total_len_55aa(pkt, len);
    if (n) return n;
    return packet_total_len_6699(pkt, len);
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)tpcb;
    tuya_conn_t *c = (tuya_conn_t *)arg;
    c->err = err;
    c->connected = (err == ERR_OK);
    c->done = true;
    return ERR_OK;
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    tuya_conn_t *c = (tuya_conn_t *)arg;
    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        c->err = err;
        c->done = true;
        return ERR_OK;
    }
    if (!p) {
        c->done = true;
        return ERR_OK;
    }

    uint16_t copy_len = p->tot_len;
    if ((uint32_t)c->rx_len + copy_len > MAX_RX) {
        copy_len = (uint16_t)(MAX_RX - c->rx_len);
    }
    if (copy_len) {
        pbuf_copy_partial(p, c->rx_buf + c->rx_len, copy_len, 0);
        c->rx_len = (uint16_t)(c->rx_len + copy_len);
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    if (packet_total_len(c->rx_buf, c->rx_len) > 0) {
        c->done = true;
    }
    return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err) {
    tuya_conn_t *c = (tuya_conn_t *)arg;
    c->err = err;
    c->done = true;
    c->pcb = NULL;
}

static void conn_close(tuya_conn_t *c) {
    if (!c->pcb) return;
    cyw43_arch_lwip_begin();
    tcp_arg(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
    tcp_err(c->pcb, NULL);
    err_t e = tcp_close(c->pcb);
    if (e != ERR_OK) tcp_abort(c->pcb);
    c->pcb = NULL;
    cyw43_arch_lwip_end();
}

static bool wait_done(tuya_conn_t *c, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!c->done && absolute_time_diff_us(get_absolute_time(), deadline) > 0) {
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(WAIT_STEP_MS));
    }
    return c->done;
}

static tuya_err_t conn_open(tuya_conn_t *c) {
    memset(c, 0, sizeof(*c));
    c->err = ERR_OK;

    ip_addr_t ip;
    if (!ipaddr_aton(LAMP_IP, &ip)) return TUYA_ERR_CONNECT;

    cyw43_arch_lwip_begin();
    c->pcb = tcp_new_ip_type(IP_GET_TYPE(&ip));
    if (!c->pcb) {
        cyw43_arch_lwip_end();
        return TUYA_ERR_SOCKET;
    }

    tcp_arg(c->pcb, c);
    tcp_recv(c->pcb, tcp_recv_cb);
    tcp_err(c->pcb, tcp_err_cb);
    tcp_nagle_disable(c->pcb);

    c->done = false;
    err_t e = tcp_connect(c->pcb, &ip, TUYA_PORT, tcp_connected_cb);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        conn_close(c);
        return TUYA_ERR_CONNECT;
    }

    if (!wait_done(c, TIMEOUT_MS) || !c->connected) {
        printf("[Tuya] connect falhou err=%d\n", (int)c->err);
        conn_close(c);
        return TUYA_ERR_CONNECT;
    }
    printf("[Tuya] connect ok\n");
    return TUYA_OK;
}

static tuya_err_t conn_send_wait(tuya_conn_t *c, const uint8_t *pkt, uint16_t pkt_len,
                                 bool wait_response, uint32_t timeout_ms) {
    if (!c->pcb) return TUYA_ERR_SOCKET;

    c->rx_len = 0;
    c->done = false;
    c->err = ERR_OK;

    cyw43_arch_lwip_begin();
    err_t e = tcp_write(c->pcb, pkt, pkt_len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) {
        e = tcp_output(c->pcb);
    }
    cyw43_arch_lwip_end();
    if (e != ERR_OK) return TUYA_ERR_SEND;

    if (!wait_response) {
        sleep_ms(30);
        return TUYA_OK;
    }

    if (!wait_done(c, timeout_ms)) return TUYA_ERR_SEND;
    if (c->err != ERR_OK) return TUYA_ERR_SEND;
    if (!packet_total_len(c->rx_buf, c->rx_len)) {
        printf("[Tuya] resposta invalida/timeout rx_len=%u", (unsigned)c->rx_len);
        if (c->rx_len >= 4) {
            printf(" head=%02X%02X%02X%02X",
                   c->rx_buf[0], c->rx_buf[1], c->rx_buf[2], c->rx_buf[3]);
        }
        printf("\n");
        return TUYA_ERR_SEND;
    }
    return TUYA_OK;
}

static int build_pkt_55aa(uint8_t *out, uint32_t cmd, const uint8_t *payload, uint16_t payload_len) {
    uint32_t len_field = (uint32_t)payload_len + 8u;
    uint32_t total = 16u + len_field;
    if (total > MAX_PKT) return -1;

    put_u32be(out + 0, TUYA_PREFIX_55AA);
    put_u32be(out + 4, seq_no++);
    put_u32be(out + 8, cmd);
    put_u32be(out + 12, len_field);
    memcpy(out + 16, payload, payload_len);

    uint32_t crc = crc32(out, 16u + payload_len);
    put_u32be(out + 16 + payload_len, crc);
    put_u32be(out + 16 + payload_len + 4, TUYA_SUFFIX_55AA);
    return (int)total;
}

static int build_pkt_6699(uint8_t *out, uint32_t cmd,
                          const uint8_t key[16],
                          const uint8_t *payload, uint16_t payload_len) {
    uint8_t header[18];
    uint8_t iv[12];
    uint8_t tag[16];
    uint8_t cipher[512];

    put_u32be(header + 0, TUYA_PREFIX_6699);
    put_u16be(header + 4, 0);
    put_u32be(header + 6, seq_no++);
    put_u32be(header + 10, cmd);
    put_u32be(header + 14, (uint32_t)payload_len + 28u);

    for (int i = 0; i < 12; i++) {
        iv[i] = (uint8_t)(get_rand_32() & 0xFFu);
    }

    if (!aes_gcm_encrypt(key, iv, header + 4, 14, payload, payload_len, cipher, tag)) {
        return -1;
    }

    uint32_t total = 18u + 12u + payload_len + 16u + 4u;
    if (total > MAX_PKT) return -1;

    uint8_t *p = out;
    memcpy(p, header, 18); p += 18;
    memcpy(p, iv, 12); p += 12;
    memcpy(p, cipher, payload_len); p += payload_len;
    memcpy(p, tag, 16); p += 16;
    put_u32be(p, TUYA_SUFFIX_6699); p += 4;
    return (int)(p - out);
}

static bool decode_pkt_55aa(const uint8_t *pkt, uint16_t len, tuya_pkt_decoded_t *d) {
    uint16_t total = packet_total_len_55aa(pkt, len);
    if (!total) return false;

    uint32_t suffix = get_u32be(pkt + total - 4);
    if (suffix != TUYA_SUFFIX_55AA) return false;

    uint32_t recv_crc = get_u32be(pkt + total - 8);
    uint32_t calc_crc = crc32(pkt, total - 8);
    if (recv_crc != calc_crc) return false;

    d->cmd = get_u32be(pkt + 8);
    d->payload = pkt + 16;
    d->payload_len = (uint16_t)(total - 16 - 8);
    return true;
}

static bool decode_pkt_6699(const uint8_t *pkt, uint16_t len,
                            const uint8_t key[16], uint8_t *plain_buf,
                            tuya_pkt_decoded_t *d) {
    uint16_t total = packet_total_len_6699(pkt, len);
    if (!total) return false;
    if (get_u32be(pkt + total - 4) != TUYA_SUFFIX_6699) return false;

    uint16_t payload_len = (uint16_t)(total - 18 - 12 - 16 - 4);
    const uint8_t *iv = pkt + 18;
    const uint8_t *cipher = pkt + 30;
    const uint8_t *tag = pkt + total - 4 - 16;
    const uint8_t *aad = pkt + 4;

    if (!aes_gcm_decrypt(key, iv, aad, 14, cipher, payload_len, tag, plain_buf)) {
        return false;
    }

    d->cmd = get_u32be(pkt + 10);
    d->payload = plain_buf;
    d->payload_len = payload_len;
    return true;
}

static bool extract_json_printable(const uint8_t *in, uint16_t in_len, char *out, size_t out_sz) {
    size_t w = 0;
    for (uint16_t i = 0; i < in_len && w + 1 < out_sz; i++) {
        uint8_t ch = in[i];
        if (ch >= 32 && ch <= 126) out[w++] = (char)ch;
    }
    out[w] = '\0';
    return w > 0;
}

static bool parse_dp_power_from_json(const char *json, bool *on) {
    const char *p_true = strstr(json, "\"20\":true");
    const char *p_false = strstr(json, "\"20\":false");
    if (p_true && (!p_false || p_true < p_false)) {
        *on = true;
        return true;
    }
    if (p_false) {
        *on = false;
        return true;
    }
    return false;
}

static bool decode_payload_55aa_to_text(const uint8_t *payload, uint16_t payload_len,
                                        char *out, size_t out_sz) {
    const uint8_t *enc = payload;
    uint16_t enc_len = payload_len;
    uint8_t plain[512];

    if (payload_len > 15 && memcmp(payload, "3.3", 3) == 0) {
        enc = payload + 15;
        enc_len = (uint16_t)(payload_len - 15);
    }
    if (enc_len == 0 || (enc_len % 16) != 0 || enc_len > sizeof(plain)) return false;

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_dec(&aes, (const unsigned char *)TUYA_LOCAL_KEY, 128) != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }
    for (uint16_t i = 0; i < enc_len; i += 16) {
        if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, enc + i, plain + i) != 0) {
            mbedtls_aes_free(&aes);
            return false;
        }
    }
    mbedtls_aes_free(&aes);

    if (enc_len > 0) {
        uint8_t pad = plain[enc_len - 1];
        if (pad > 0 && pad <= 16 && pad <= enc_len) {
            bool valid = true;
            for (uint8_t i = 0; i < pad; i++) {
                if (plain[enc_len - 1 - i] != pad) {
                    valid = false;
                    break;
                }
            }
            if (valid) enc_len = (uint16_t)(enc_len - pad);
        }
    }
    return extract_json_printable(plain, enc_len, out, out_sz);
}

static void make_json_legacy(char *buf, size_t sz, const char *dps_body) {
    uint32_t ts = fake_timestamp();
    snprintf(buf, sz,
             "{\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%lu\",\"dps\":{%s}}",
             TUYA_DEVICE_ID, TUYA_DEVICE_ID, (unsigned long)ts, dps_body);
}

static void make_json_v35(char *buf, size_t sz, const char *dps_body) {
    uint32_t ts = fake_timestamp();
    snprintf(buf, sz,
             "{\"protocol\":5,\"t\":\"%lu\",\"data\":{\"dps\":{%s}}}",
             (unsigned long)ts, dps_body);
}

static bool derive_session_key_v35(const uint8_t local_nonce[16], const uint8_t remote_nonce[16],
                                   uint8_t session_key[16]) {
    uint8_t mixed[16];
    uint8_t tag[16];
    uint8_t enc[16];
    uint8_t iv[12];

    for (int i = 0; i < 16; i++) mixed[i] = (uint8_t)(local_nonce[i] ^ remote_nonce[i]);
    memcpy(iv, local_nonce, 12);

    if (!aes_gcm_encrypt((const uint8_t *)TUYA_LOCAL_KEY, iv, NULL, 0, mixed, 16, enc, tag)) {
        return false;
    }
    memcpy(session_key, enc, 16);
    return true;
}

static bool negotiate_session_v35(tuya_conn_t *c, uint8_t session_key[16]) {
    uint8_t pkt[MAX_PKT];
    uint8_t plain[MAX_RX];
    uint8_t local_nonce[16];
    uint8_t remote_nonce[16];
    uint8_t mac[32];
    tuya_pkt_decoded_t dec;

    memcpy(local_nonce, "0123456789abcdef", 16);

    int n = build_pkt_6699(pkt, CMD_SESS_KEY_START, (const uint8_t *)TUYA_LOCAL_KEY, local_nonce, 16);
    if (n < 0) return false;
    if (conn_send_wait(c, pkt, (uint16_t)n, true, TIMEOUT_MS) != TUYA_OK) {
        printf("[Tuya] v3.5 sess step1 sem resposta\n");
        return false;
    }

    if (!decode_pkt_6699(c->rx_buf, c->rx_len, (const uint8_t *)TUYA_LOCAL_KEY, plain, &dec)) {
        printf("[Tuya] v3.5 sess step2 decode falhou\n");
        return false;
    }
    if (dec.cmd != CMD_SESS_KEY_RESP || dec.payload_len < 48) {
        printf("[Tuya] v3.5 sess step2 cmd/len invalido cmd=%lu len=%u\n",
               (unsigned long)dec.cmd, (unsigned)dec.payload_len);
        return false;
    }

    /* Alguns firmwares incluem retcode (4 bytes) antes do payload esperado. */
    uint16_t off = 0;
    bool hmac_ok = false;
    if (!hmac_sha256((const uint8_t *)TUYA_LOCAL_KEY, 16, local_nonce, 16, mac)) return false;

    if (dec.payload_len >= 48 && memcmp(mac, dec.payload + 16, 32) == 0) {
        off = 0;
        hmac_ok = true;
    } else if (dec.payload_len >= 52 && memcmp(mac, dec.payload + 4 + 16, 32) == 0) {
        off = 4;
        hmac_ok = true;
    }
    if (!hmac_ok) {
        printf("[Tuya] v3.5 sess step2 HMAC mismatch len=%u p0=%02X%02X%02X%02X\n",
               (unsigned)dec.payload_len,
               dec.payload_len > 0 ? dec.payload[0] : 0,
               dec.payload_len > 1 ? dec.payload[1] : 0,
               dec.payload_len > 2 ? dec.payload[2] : 0,
               dec.payload_len > 3 ? dec.payload[3] : 0);
        return false;
    }
    memcpy(remote_nonce, dec.payload + off, 16);

    if (!hmac_sha256((const uint8_t *)TUYA_LOCAL_KEY, 16, remote_nonce, 16, mac)) return false;
    n = build_pkt_6699(pkt, CMD_SESS_KEY_FINISH, (const uint8_t *)TUYA_LOCAL_KEY, mac, 32);
    if (n < 0) return false;
    if (conn_send_wait(c, pkt, (uint16_t)n, false, 200) != TUYA_OK) {
        printf("[Tuya] v3.5 sess step3 envio falhou\n");
        return false;
    }

    if (!derive_session_key_v35(local_nonce, remote_nonce, session_key)) {
        printf("[Tuya] v3.5 derivacao session key falhou\n");
        return false;
    }
    printf("[Tuya] v3.5 session key OK\n");
    return true;
}

static tuya_err_t send_55aa_command(uint32_t cmd, const char *json) {
    uint8_t plain[320];
    uint8_t enc[320];
    uint8_t payload[384];
    uint8_t pkt[MAX_PKT];
    tuya_conn_t c;

    size_t json_len = strlen(json);
    memcpy(plain, json, json_len);
    size_t enc_len = aes_ecb_encrypt_padded((const uint8_t *)TUYA_LOCAL_KEY, plain, json_len, enc, sizeof(enc));
    if (!enc_len) return TUYA_ERR_ENCRYPT;

    memcpy(payload, TUYA_VER_HDR_33, 15);
    memcpy(payload + 15, enc, enc_len);
    uint16_t payload_len = (uint16_t)(15 + enc_len);

    int n = build_pkt_55aa(pkt, cmd, payload, payload_len);
    if (n < 0) return TUYA_ERR_ENCRYPT;

    tuya_err_t e = conn_open(&c);
    if (e != TUYA_OK) return e;
    e = conn_send_wait(&c, pkt, (uint16_t)n, true, TIMEOUT_MS);
    if (e == TUYA_OK) {
        tuya_pkt_decoded_t dec;
        if (!decode_pkt_55aa(c.rx_buf, c.rx_len, &dec)) e = TUYA_ERR_SEND;
    }
    conn_close(&c);
    return e;
}

static tuya_err_t send_6699_command(uint32_t cmd, const char *json, bool include_ver_hdr) {
    uint8_t payload[384];
    uint8_t pkt[MAX_PKT];
    uint8_t plain[MAX_RX];
    uint8_t session_key[16];
    tuya_pkt_decoded_t dec;
    tuya_conn_t c;

    uint16_t payload_len = 0;
    if (include_ver_hdr) {
        memcpy(payload, TUYA_VER_HDR_35, 15);
        payload_len = 15;
    }
    size_t json_len = strlen(json);
    if ((uint32_t)payload_len + json_len > sizeof(payload)) return TUYA_ERR_ENCRYPT;
    memcpy(payload + payload_len, json, json_len);
    payload_len = (uint16_t)(payload_len + json_len);

    tuya_err_t e = conn_open(&c);
    if (e != TUYA_OK) return e;

    if (!negotiate_session_v35(&c, session_key)) {
        conn_close(&c);
        return TUYA_ERR_SEND;
    }

    int n = build_pkt_6699(pkt, cmd, session_key, payload, payload_len);
    if (n < 0) {
        conn_close(&c);
        return TUYA_ERR_ENCRYPT;
    }

    e = conn_send_wait(&c, pkt, (uint16_t)n, true, TIMEOUT_MS);
    if (e == TUYA_OK) {
        if (!decode_pkt_6699(c.rx_buf, c.rx_len, session_key, plain, &dec)) {
            e = TUYA_ERR_SEND;
        }
    }

    conn_close(&c);
    return e;
}

static tuya_err_t send_set_command(const char *json_v35, const char *json_legacy) {
    tuya_err_t r;

    printf("[Tuya] try v3.5 cmd13\n");
    r = send_6699_command(CMD_SET_ALT, json_v35, true);
    printf("[Tuya] v3.5 cmd13 result=%d\n", r);
    if (r == TUYA_OK) return TUYA_OK;

    printf("[Tuya] try v3.5 cmd7\n");
    r = send_6699_command(CMD_SET, json_v35, true);
    printf("[Tuya] v3.5 cmd7 result=%d\n", r);
    if (r == TUYA_OK) return TUYA_OK;

    printf("[Tuya] try v3.3 cmd13\n");
    r = send_55aa_command(CMD_SET_ALT, json_legacy);
    printf("[Tuya] v3.3 cmd13 result=%d\n", r);
    if (r == TUYA_OK) return TUYA_OK;

    printf("[Tuya] try v3.3 cmd7\n");
    r = send_55aa_command(CMD_SET, json_legacy);
    printf("[Tuya] v3.3 cmd7 result=%d\n", r);
    return r;
}

tuya_err_t tuya_set_power(bool on) {
    char dps[32];
    char json35[256];
    char json33[256];

    snprintf(dps, sizeof(dps), "\"%d\":%s", DP_SWITCH, on ? "true" : "false");
    make_json_v35(json35, sizeof(json35), dps);
    make_json_legacy(json33, sizeof(json33), dps);

    printf("[Tuya] Power: %s\n", on ? "ON" : "OFF");
    return send_set_command(json35, json33);
}

tuya_err_t tuya_set_brightness(uint8_t pct) {
    int val = 10 + (int)(pct * 990) / 100;
    char dps[64];
    char json35[256];
    char json33[256];

    /* Atualiza somente brilho para preservar o modo/cor atual. */
    snprintf(dps, sizeof(dps), "\"%d\":%d", DP_BRIGHTNESS, val);
    make_json_v35(json35, sizeof(json35), dps);
    make_json_legacy(json33, sizeof(json33), dps);

    printf("[Tuya] Brightness: %u%% -> dp22=%d\n", pct, val);
    return send_set_command(json35, json33);
}

tuya_err_t tuya_set_color(uint16_t hue, uint8_t sat, uint8_t val) {
    uint16_t tu_h = hue;
    uint16_t tu_s = (uint16_t)(sat * 10);
    uint16_t tu_v = (uint16_t)(val * 10);
    if (tu_v < 10) tu_v = 10;

    char color_str[13];
    char dps[96];
    char json35[256];
    char json33[256];

    snprintf(color_str, sizeof(color_str), "%04X%04X%04X", tu_h, tu_s, tu_v);
    snprintf(dps, sizeof(dps), "\"%d\":\"colour\",\"%d\":\"%s\"", DP_MODE, DP_COLOR, color_str);
    make_json_v35(json35, sizeof(json35), dps);
    make_json_legacy(json33, sizeof(json33), dps);

    printf("[Tuya] Color: H=%u S=%u V=%u -> %s\n", hue, sat, val, color_str);
    return send_set_command(json35, json33);
}

tuya_err_t tuya_set_white(uint8_t brightness_pct, uint8_t temp_pct) {
    int br = 10 + (int)(brightness_pct * 990) / 100;
    int tmp = (int)(temp_pct * 1000) / 100;
    char dps[96];
    char json35[256];
    char json33[256];

    snprintf(dps, sizeof(dps), "\"%d\":\"white\",\"%d\":%d,\"%d\":%d",
             DP_MODE, DP_BRIGHTNESS, br, DP_COLOR_TEMP, tmp);
    make_json_v35(json35, sizeof(json35), dps);
    make_json_legacy(json33, sizeof(json33), dps);
    return send_set_command(json35, json33);
}

tuya_err_t tuya_get_power_state(bool *on) {
    tuya_conn_t c;
    tuya_pkt_decoded_t dec;
    uint8_t pkt[MAX_PKT];
    uint8_t plain[MAX_RX];
    uint8_t session_key[16];
    char json[192];
    char text[512];
    int n;

    if (!on) return TUYA_ERR_SEND;

    snprintf(json, sizeof(json),
             "{\"gwId\":\"%s\",\"devId\":\"%s\",\"uid\":\"%s\",\"t\":\"%lu\"}",
             TUYA_DEVICE_ID, TUYA_DEVICE_ID, TUYA_DEVICE_ID,
             (unsigned long)fake_timestamp());

    /* Tenta primeiro protocolo 3.5 */
    if (conn_open(&c) == TUYA_OK) {
        if (negotiate_session_v35(&c, session_key)) {
            uint8_t payload[256];
            uint16_t payload_len = 15;
            memcpy(payload, TUYA_VER_HDR_35, 15);
            if (payload_len + strlen(json) < sizeof(payload)) {
                memcpy(payload + payload_len, json, strlen(json));
                payload_len = (uint16_t)(payload_len + strlen(json));
                n = build_pkt_6699(pkt, CMD_STATUS, session_key, payload, payload_len);
                if (n >= 0 && conn_send_wait(&c, pkt, (uint16_t)n, true, TIMEOUT_MS) == TUYA_OK) {
                    if (decode_pkt_6699(c.rx_buf, c.rx_len, session_key, plain, &dec) &&
                        extract_json_printable(dec.payload, dec.payload_len, text, sizeof(text)) &&
                        parse_dp_power_from_json(text, on)) {
                        conn_close(&c);
                        printf("[Tuya] Estado remoto: %s (v3.5)\n", *on ? "ON" : "OFF");
                        return TUYA_OK;
                    }
                }
            }
        }
        conn_close(&c);
    }

    /* Fallback protocolo 3.3 */
    {
        uint8_t enc[256];
        uint8_t payload[320];
        size_t enc_len = aes_ecb_encrypt_padded((const uint8_t *)TUYA_LOCAL_KEY,
                                                (const uint8_t *)json, strlen(json),
                                                enc, sizeof(enc));
        if (!enc_len) return TUYA_ERR_ENCRYPT;
        memcpy(payload, TUYA_VER_HDR_33, 15);
        memcpy(payload + 15, enc, enc_len);
        n = build_pkt_55aa(pkt, CMD_STATUS, payload, (uint16_t)(15 + enc_len));
        if (n < 0) return TUYA_ERR_ENCRYPT;
    }

    if (conn_open(&c) != TUYA_OK) return TUYA_ERR_CONNECT;
    if (conn_send_wait(&c, pkt, (uint16_t)n, true, TIMEOUT_MS) != TUYA_OK) {
        conn_close(&c);
        return TUYA_ERR_SEND;
    }
    if (!decode_pkt_55aa(c.rx_buf, c.rx_len, &dec)) {
        conn_close(&c);
        return TUYA_ERR_SEND;
    }
    if (!decode_payload_55aa_to_text(dec.payload, dec.payload_len, text, sizeof(text))) {
        conn_close(&c);
        return TUYA_ERR_SEND;
    }
    if (!parse_dp_power_from_json(text, on)) {
        conn_close(&c);
        return TUYA_ERR_SEND;
    }

    conn_close(&c);
    printf("[Tuya] Estado remoto: %s (v3.3)\n", *on ? "ON" : "OFF");
    return TUYA_OK;
}

tuya_err_t tuya_heartbeat(void) {
    char json[128];
    snprintf(json, sizeof(json), "{\"gwId\":\"%s\",\"devId\":\"%s\"}",
             TUYA_DEVICE_ID, TUYA_DEVICE_ID);

    tuya_err_t r = send_6699_command(CMD_HEARTBEAT, json, false);
    if (r == TUYA_OK) return TUYA_OK;
    return send_55aa_command(CMD_HEARTBEAT, json);
}
