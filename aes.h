#ifndef AES_H
#define AES_H
/*
 * aes.h — AES-128-ECB (domínio público, baseado em tiny-AES-c por kokke)
 * Implementação mínima apenas para encriptação ECB usada no protocolo Tuya.
 */
#include <stdint.h>
#include <stddef.h>

#define AES_BLOCKLEN  16   /* Tamanho do bloco: 128 bits */
#define AES_KEYLEN    16   /* Tamanho da chave: 128 bits */
#define AES_keyExpSize 176

typedef struct {
    uint8_t round_key[AES_keyExpSize];
} AES_ctx;

/* Inicializa o contexto com a chave de 16 bytes */
void AES_init_ctx(AES_ctx *ctx, const uint8_t *key);

/* Encripta um bloco de 16 bytes in-place (ECB) */
void AES_ECB_encrypt(const AES_ctx *ctx, uint8_t *buf);

/*
 * Helpers de nível mais alto
 */

/* Padding PKCS7 + encriptação de um buffer (deve ter espaço para padding)
 * Retorna o novo tamanho (múltiplo de 16). */
size_t aes_ecb_encrypt_padded(const uint8_t *key,
                               const uint8_t *plaintext, size_t len,
                               uint8_t *out, size_t out_max);

#endif /* AES_H */
