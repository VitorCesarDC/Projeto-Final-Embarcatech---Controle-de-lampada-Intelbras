/*
 * lwipopts.h — Configuração do lwIP para BitDogLab (RP2040 + CYW43)
 *
 * OBRIGATÓRIO: este arquivo deve estar no diretório do projeto.
 * Habilita a API de sockets BSD usada pelo módulo Tuya.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ── Configurações obrigatórias para Pico W ─────────────────────────── */
#define NO_SYS                   1
#define LWIP_SOCKET              0
#define LWIP_NETCONN             0
#define MEM_LIBC_MALLOC          0

/* ── Memória ─────────────────────────────────────────────────────────── */
#define MEM_ALIGNMENT            4
#define MEM_SIZE                 4000
#define MEMP_NUM_TCP_PCB         5
#define MEMP_NUM_TCP_PCB_LISTEN  2
#define MEMP_NUM_PBUF            20
#define MEMP_NUM_UDP_PCB         4
#define PBUF_POOL_SIZE           12
#define PBUF_POOL_BUFSIZE        256

/* ── TCP ─────────────────────────────────────────────────────────────── */
#define LWIP_TCP                 1
#define TCP_MSS                  1460
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_WND                 2048

/* ── UDP / DHCP / DNS ────────────────────────────────────────────────── */
#define LWIP_UDP                 1
#define LWIP_DHCP                1
#define LWIP_DNS                 1

/* ── Checksum (hardware quando disponível) ───────────────────────────── */
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0

/* ── Debug (comente para release) ───────────────────────────────────── */
/* #define LWIP_DEBUG 1 */
/* #define TCP_DEBUG  LWIP_DBG_ON */

#endif /* LWIPOPTS_H */
