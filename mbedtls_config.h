#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

#include "mbedtls/mbedtls_config.h"

/* RP2040 não usa fontes de entropia de Unix/Windows. */
#undef MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_PLATFORM_ENTROPY

/* Evita dependência de clock de plataforma no SDK embarcado. */
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_TIMING_C

/* Evita APIs de filesystem não disponíveis no toolchain embarcado. */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SHA3_C

#endif
