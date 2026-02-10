// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.
// Copied from RealSense ID SDK samples for Simon Says secure build.

#pragma once

#include "RealSenseID/SignatureCallback.h"
#include "RealSenseID/FaceAuthenticator.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/asn1.h"
#include "mbedtls/asn1write.h"

namespace RealSenseID
{
namespace Samples
{
class SignHelper : public RealSenseID::SignatureCallback
{
public:
    SignHelper();
    ~SignHelper();

    bool Sign(const unsigned char* buffer, const unsigned int buffer_len, unsigned char* out_sig) override;
    bool Verify(const unsigned char* buffer, const unsigned int buffer_len, const unsigned char* sig, const unsigned int sign_len) override;

    void UpdateDevicePubKey(const unsigned char* pubKey);
    const unsigned char* GetHostPubKey() const;

private:
    bool _initialized = false;
    mbedtls_entropy_context _entropy;
    mbedtls_ctr_drbg_context _ctr_drbg;
    mbedtls_ecdsa_context _ecdsa_host_context;
    mbedtls_ecdsa_context _ecdsa_device_context;
};
} // namespace Samples
} // namespace RealSenseID
