#pragma once

// Fetch a secret from AWS Secrets Manager and return its key-value pairs.
//
// USAGE REQUIREMENTS
// ------------------
// Consumers must link yggdrasil (which compiles the implementation).
// aws-sdk-cpp (features: core, secretsmanager) must be in vcpkg.json and
// linked via yggdrasil's CMakeLists. Aws::InitAPI must have been called
// before the first call to ygg::secrets::fetch.
//
// LOCAL DEV FALLBACK
// ------------------
// If the environment variable BPT_ENV=local, the secret is read from a JSON file
// at ~/.bpt-secrets/<secret_name>.json (forward slashes in secret_name are kept
// as path separators, so "bpt/testnet/OKX" resolves to
// ~/.bpt-secrets/bpt/testnet/OKX.json).
//
// The JSON format matches the Secrets Manager plaintext exactly:
//   { "OKX_API_KEY": "...", "OKX_SECRET_KEY": "...", "OKX_PASSPHRASE": "..." }

#include <map>
#include <string>

namespace ygg::secrets {

std::map<std::string, std::string> fetch(const std::string& secret_name);

}  // namespace ygg::secrets
