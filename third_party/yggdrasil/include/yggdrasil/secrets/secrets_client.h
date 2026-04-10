#pragma once

// Fetch a secret from AWS Secrets Manager and return its key-value pairs.
//
// USAGE REQUIREMENTS
// ------------------
// Consumers must have aws-sdk-cpp (features: core, secretsmanager) in their
// vcpkg.json and link aws-cpp-sdk-core + aws-cpp-sdk-secretsmanager.
// Aws::InitAPI must have been called before the first call to ygg::secrets::fetch.
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

#include <aws/core/utils/json/JsonSerializer.h>
#include <aws/secretsmanager/SecretsManagerClient.h>
#include <aws/secretsmanager/model/GetSecretValueRequest.h>
#include <cstdlib>
#include <fstream>
#include <map>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace ygg::secrets {

inline std::map<std::string, std::string> fetch(const std::string& secret_name) {
    std::string json_str;

    const char* bpt_env = std::getenv("BPT_ENV");
    const bool local = bpt_env && std::string_view(bpt_env) == "local";

    if (local) {
        const char* home = std::getenv("HOME");
        if (!home)
            throw std::runtime_error("[secrets] HOME not set");
        const std::string path = std::string(home) + "/.bpt-secrets/" + secret_name + ".json";
        std::ifstream f(path);
        if (!f)
            throw std::runtime_error("[secrets] Local secret file not found: " + path);
        json_str.assign(std::istreambuf_iterator<char>(f), {});
        spdlog::debug("[secrets] Loaded '{}' from local file", secret_name);
    } else {
        Aws::SecretsManager::SecretsManagerClient client;
        auto req = Aws::SecretsManager::Model::GetSecretValueRequest().WithSecretId(secret_name.c_str());
        auto outcome = client.GetSecretValue(req);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error(std::string("[secrets] Failed to fetch '") + secret_name +
                                     "': " + outcome.GetError().GetMessage().c_str());
        }
        json_str = outcome.GetResult().GetSecretString().c_str();
        spdlog::info("[secrets] Fetched secret '{}'", secret_name);
    }

    Aws::Utils::Json::JsonValue jv(json_str.c_str());
    if (!jv.WasParseSuccessful()) {
        throw std::runtime_error(std::string("[secrets] Failed to parse JSON for '") + secret_name +
                                 "': " + jv.GetErrorMessage().c_str());
    }

    std::map<std::string, std::string> result;
    for (const auto& kv : jv.View().GetAllObjects())
        result[kv.first.c_str()] = kv.second.AsString().c_str();
    return result;
}

}  // namespace ygg::secrets
