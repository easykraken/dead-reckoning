#ifndef OTA_PUBLIC_KEY_H
#define OTA_PUBLIC_KEY_H

// SAMPLE public key for compile-time testing only.
// Run `python scripts/ota-tool.py generate` to create your own keypair;
// that command will overwrite this file with your real public key.
const char OTA_PUBLIC_KEY_PEM[] =
"-----BEGIN PUBLIC KEY-----\n"
"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEaYNvBEOBoNlpfV0J/elMWTvNzCMe\n"
"ScCrN4NsO8gvJTXsndhSxczSJVzfe1pkW75HSboBXKIyovcs9TknXVABGw==\n"
"-----END PUBLIC KEY-----\n";

#endif // OTA_PUBLIC_KEY_H
