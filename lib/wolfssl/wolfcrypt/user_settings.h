#ifndef _WIN_USER_SETTINGS_H_
#define _WIN_USER_SETTINGS_H_

#define OPENSSL_EXTRA

#define NO_MULTIBYTE_PRINT
#define NO_PSK

#define WOLFSSL_DES_ECB
#define WOLFSSL_RIPEMD
#define WOLFSSL_SNIFFER
#define WOLFSSL_SHA224
#define WOLFSSL_SHA384
#define WOLFSSL_SHA512
#define WOLFSSL_TLS13
#define WOLFSSL_SYS_CA_CERTS

#define HAVE_TLS_EXTENSIONS
#define HAVE_SECURE_RENEGOTIATION
#define HAVE_EXTENDED_MASTER
#define HAVE_AESGCM
#define HAVE_SUPPORTED_CURVES
#define HAVE_ECC
#define HAVE_SNI
#define HAVE_FFDHE_2048
#define HAVE_FFDHE_3072
#define HAVE_FFDHE_4096
#define HAVE_FFDHE_6144
#define HAVE_FFDHE_8192
#define HAVE_HKDF

#define ECC_SHAMIR
#define ECC_TIMING_RESISTANT

#define WC_RSA_BLINDING
#define WC_RSA_PSS

#include <limits.h>

#if !defined(SIZEOF_LONG) && defined(ULONG_MAX) && (ULONG_MAX == 0xffffffffUL)
#define SIZEOF_LONG 4
#endif
#if !defined(SIZEOF_LONG_LONG) && defined(ULLONG_MAX) && (ULLONG_MAX == 0xffffffffffffffffULL)
#define SIZEOF_LONG_LONG 8
#endif

#endif /* _WIN_USER_SETTINGS_H_ */