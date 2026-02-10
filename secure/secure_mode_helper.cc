// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.
// Copied from RealSense ID SDK samples for Simon Says secure build.

#include "secure_mode_helper.h"
#include "mbedtls/sha256.h"
#include <cstring>

#ifndef MBEDTLS_ASN1_CHK_ADD
#define MBEDTLS_ASN1_CHK_ADD(g, f) do { if( ( ret = f ) < 0 ) return( ret ); else g += ret; } while( 0 )
#endif

#define SHA_256_DIGEST_SIZE_BYTES 32
#define PRI_KEY_SIZE              32
#define PUB_X_Y_SIZE              32
#define PUB_KEY_SIZE              64

static unsigned char SAMPLE_HOST_PRI_KEY[PRI_KEY_SIZE] = {0xb9, 0xad, 0xfe, 0x0e, 0x6d, 0xd4, 0xfb, 0x6f, 0x76, 0xdf, 0x53,
                                                          0x92, 0x87, 0x4e, 0x58, 0x39, 0xdd, 0x51, 0xd1, 0xaa, 0x79, 0x94,
                                                          0x5e, 0xa8, 0x36, 0x8f, 0xb5, 0xdf, 0xa8, 0x28, 0x26, 0x53};

static unsigned char SAMPLE_HOST_PUB_KEY[PUB_KEY_SIZE] = {
    0xa9, 0x19, 0xcd, 0x93, 0x0f, 0xfb, 0x3e, 0x95, 0x5e, 0xf2, 0x94, 0xa5, 0x90, 0xca, 0x0e, 0x82, 0x19, 0x08, 0x72, 0x23, 0x8d, 0xec,
    0x49, 0x97, 0xb4, 0x7d, 0x1c, 0x81, 0x6f, 0x18, 0x4e, 0xe7, 0x86, 0xf5, 0x69, 0x7a, 0xde, 0x6a, 0x69, 0xac, 0x64, 0xa2, 0xcd, 0xdf,
    0x8c, 0xe1, 0x7a, 0xea, 0x4d, 0xf7, 0xc6, 0xd6, 0x10, 0xa2, 0xc5, 0x33, 0xe6, 0x0c, 0x2f, 0xce, 0x55, 0x6e, 0x1c, 0xf8};

static unsigned char DEVICE_PUB_KEY[PUB_KEY_SIZE] = {0};

namespace RealSenseID
{
namespace Samples
{
SignHelper::SignHelper()
{
    mbedtls_entropy_init(&_entropy);
    mbedtls_ctr_drbg_init(&_ctr_drbg);
    mbedtls_ecdsa_init(&_ecdsa_host_context);
    mbedtls_ecdsa_init(&_ecdsa_device_context);

    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;
    if (mbedtls_ecdsa_genkey(&_ecdsa_host_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.Q.X, SAMPLE_HOST_PUB_KEY, PUB_X_Y_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_host_context.Q.Y, SAMPLE_HOST_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
        return;
    if (mbedtls_ecdsa_genkey(&_ecdsa_device_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    /* Only load device public key if we have one (non-zero); otherwise leave from genkey to avoid mbedtls issues with zero MPIs */
    bool have_device_key = false;
    for (size_t i = 0; i < PUB_KEY_SIZE; i++) { if (DEVICE_PUB_KEY[i] != 0) { have_device_key = true; break; } }
    if (have_device_key) {
        if (mbedtls_mpi_read_binary(&_ecdsa_device_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
            return;
        if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.X, DEVICE_PUB_KEY, PUB_X_Y_SIZE))
            return;
        if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.Y, DEVICE_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
            return;
    }
    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;
    _initialized = true;
}

SignHelper::~SignHelper()
{
    mbedtls_entropy_free(&_entropy);
    mbedtls_ctr_drbg_free(&_ctr_drbg);
    mbedtls_ecdsa_free(&_ecdsa_host_context);
    mbedtls_ecdsa_free(&_ecdsa_device_context);
}

bool SignHelper::Sign(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig)
{
    if (!_initialized) return false;
    unsigned char digest[SHA_256_DIGEST_SIZE_BYTES];
    int ret = mbedtls_sha256_ret(buffer, buffer_len, digest, 0);
    if (ret != 0) return false;
    unsigned char signature[MBEDTLS_ECDSA_MAX_LEN];
    size_t sign_len, lenTag;
    ret = mbedtls_ecdsa_write_signature(&_ecdsa_host_context, MBEDTLS_MD_SHA256, digest, SHA_256_DIGEST_SIZE_BYTES, signature, &sign_len,
                                        mbedtls_ctr_drbg_random, &_ctr_drbg);
    if (ret != 0) return false;
    unsigned char* ptrSig = signature;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    ret = mbedtls_asn1_get_tag(&ptrSig, signature + sign_len, &lenTag, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (!ret) {
        ret = mbedtls_asn1_get_mpi(&ptrSig, signature + sign_len, &r);
        if (!ret) {
            ret = mbedtls_asn1_get_mpi(&ptrSig, signature + sign_len, &s);
            if (!ret) {
                ret = mbedtls_mpi_write_binary(&r, out_sig, PUB_X_Y_SIZE);
                if (!ret) ret = mbedtls_mpi_write_binary(&s, out_sig + PUB_X_Y_SIZE, PUB_X_Y_SIZE);
            }
        }
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret == 0;
}

bool SignHelper::Verify(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig, const unsigned int sign_len)
{
    if (!_initialized) return false;
    (void)sign_len;
    unsigned char buf[MBEDTLS_ECDSA_MAX_LEN];
    unsigned char* p = buf + sizeof(buf);
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    int ret = mbedtls_mpi_read_binary(&r, sig, PUB_X_Y_SIZE);
    if (!ret) {
        ret = mbedtls_mpi_read_binary(&s, sig + PUB_X_Y_SIZE, PUB_X_Y_SIZE);
        if (!ret) {
            size_t len = 0;
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_mpi(&p, buf, &s));
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_mpi(&p, buf, &r));
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, len));
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_tag(&p, buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE));
            unsigned char digest[SHA_256_DIGEST_SIZE_BYTES];
            ret = mbedtls_sha256_ret(buffer, buffer_len, digest, 0);
            if (ret == 0)
                ret = mbedtls_ecdsa_read_signature(&_ecdsa_device_context, digest, SHA_256_DIGEST_SIZE_BYTES, p, len);
        }
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ret == 0;
}

void SignHelper::UpdateDevicePubKey(const unsigned char* pubKey)
{
    ::memcpy(DEVICE_PUB_KEY, pubKey, PUB_KEY_SIZE);
    if (mbedtls_ecdsa_genkey(&_ecdsa_device_context, MBEDTLS_ECP_DP_SECP256R1, mbedtls_ctr_drbg_random, &_ctr_drbg))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.d, SAMPLE_HOST_PRI_KEY, PRI_KEY_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.X, DEVICE_PUB_KEY, PUB_X_Y_SIZE))
        return;
    if (mbedtls_mpi_read_binary(&_ecdsa_device_context.Q.Y, DEVICE_PUB_KEY + PUB_X_Y_SIZE, PUB_X_Y_SIZE))
        return;
    if (mbedtls_ctr_drbg_seed(&_ctr_drbg, mbedtls_entropy_func, &_entropy, NULL, 0))
        return;
}

const unsigned char* SignHelper::GetHostPubKey() const
{
    return SAMPLE_HOST_PUB_KEY;
}

} // namespace Samples
} // namespace RealSenseID
