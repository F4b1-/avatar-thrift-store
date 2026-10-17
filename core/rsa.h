// RSA signature check, PKCS#1 v1.5 over a SHA-1 hash, as the console signs
// profiles (README, "Key findings: Profile signatures"). Verification only: signing
// is done by the console's own key, never by this code.
#pragma once

#include "types.h"

// True when signature^exponent mod modulus is 00 01 FF..FF 00 <SHA-1 DigestInfo>
// <hash>. Modulus and signature are big-endian, `modulusBytes` long (at most
// 512); a signature not below the modulus is refused.
bool RsaVerifySha1(const u8* modulus, u32 modulusBytes, u32 exponent, const u8* signature, const u8 hash[20]);
