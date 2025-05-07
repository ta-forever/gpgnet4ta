#include "ServerEncrypt.h"

#include "cryptopp/aes.h"
#include "cryptopp/modes.h"
#include "cryptopp/osrng.h"
#include "cryptopp/rsa.h"
#include "cryptopp/base64.h"
#include "cryptopp/integer.h"
#include "cryptopp/files.h"
#include "cryptopp/hex.h"

std::string tafencrypt::ServerEncrypt(const std::string& text)
{
    CryptoPP::AutoSeededRandomPool rng;

#define AES_KEY_SIZE 16
#define AES_BLOCK_SIZE 16

    /* Generate random AES Key and Initialisation vector */
    CryptoPP::SecByteBlock aes_key(AES_KEY_SIZE);
    CryptoPP::SecByteBlock iv(AES_BLOCK_SIZE);

    rng.GenerateBlock(aes_key, AES_KEY_SIZE);
    rng.GenerateBlock(iv, AES_BLOCK_SIZE);

    /* Base64 encode IV */
    std::string iv_b64;
    {
        CryptoPP::StringSource s(iv, AES_BLOCK_SIZE, true,
            new CryptoPP::Base64Encoder(
                new CryptoPP::StringSink(iv_b64), false
            )
        );
    }
    /* the server expects 24 bytes */
    assert(iv_b64.size() == 24);

    /* create the RSA Public key from the UID_PUBKEY_BYTES #define */
    uint8_t pubkey_bytes[256] = { UID_PUBKEY_BYTES };
    CryptoPP::RSA::PublicKey publicKey;
    CryptoPP::Integer modulus;
    modulus.Decode(pubkey_bytes, 256);
    publicKey.Initialize(modulus, 65537);

    //return "OK";
    /* encrypt the AES key and encode the encryptes key to base64 */
    std::string aes_key_encrypted_base64;
    {
        CryptoPP::RSAES_PKCS1v15_Encryptor e(publicKey);
        CryptoPP::StringSource s(aes_key, AES_KEY_SIZE, true,
            new CryptoPP::PK_EncryptorFilter(rng, e,
                new CryptoPP::Base64Encoder(
                    new CryptoPP::StringSink(aes_key_encrypted_base64), false /*insertLineBreaks*/
                )
            )
        );
    }
    /* now encrypt the JSON string with AES */
    std::string text_encrypted_b64;
    {
        CryptoPP::CBC_Mode< CryptoPP::AES >::Encryption e;
        e.SetKeyWithIV(aes_key, AES_KEY_SIZE, iv);
        CryptoPP::StringSource s(text, true,
            new CryptoPP::StreamTransformationFilter(e,
                new CryptoPP::Base64Encoder(
                    new CryptoPP::StringSink(text_encrypted_b64), false /*insertLineBreaks*/
                )
            )
        );
    }
    //return "OK";

    /* the number of padding bytes (AES can only encrypt a multiple of 16 bytes) */
    unsigned char paddingSize = 16 - (text.size() % 16);

    std::string final_output;
    {
        /* the final bytearray consists of the
            1 byte paddingSize
            24 bytes IV (base64)
            X bytes AES encrypted JSON (base64)
            344 bytes RSA encrypted AES key (base64) */
        CryptoPP::Base64Encoder b(
            new CryptoPP::StringSink(final_output), false /*insertLineBreaks*/
        );
        b.Put(paddingSize);

#ifdef _WIN32
        b.Put(reinterpret_cast<const byte*>(iv_b64.c_str()), iv_b64.size());
        b.Put(reinterpret_cast<const byte*>(text_encrypted_b64.c_str()), text_encrypted_b64.size());
        b.Put(reinterpret_cast<const byte*>(aes_key_encrypted_base64.c_str()), aes_key_encrypted_base64.size());
#else
        b.Put(reinterpret_cast<const CryptoPP::byte*>(iv_b64.c_str()), iv_b64.size());
        b.Put(reinterpret_cast<const CryptoPP::byte*>(text_encrypted_b64.c_str()), text_encrypted_b64.size());
        b.Put(reinterpret_cast<const CryptoPP::byte*>(aes_key_encrypted_base64.c_str()), aes_key_encrypted_base64.size());
#endif
        b.MessageEnd();
    }

    return final_output;
}
