#include "yggdrasil/secrets/secrets_client.h"

#include <aws/core/utils/json/JsonSerializer.h>
#include <aws/secretsmanager/SecretsManagerClient.h>
#include <aws/secretsmanager/model/GetSecretValueRequest.h>
#include <cstdlib>
#include <fstream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <yggdrasil/logging.h>

namespace ygg::secrets {

std::map<std::string, std::string> fetch(const std::string& secret_name) {
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
        ygg::log::debug("[secrets] Loaded '{}' from local file", secret_name);
    } else {
        Aws::SecretsManager::SecretsManagerClient client;
        auto req = Aws::SecretsManager::Model::GetSecretValueRequest().WithSecretId(secret_name.c_str());
        auto outcome = client.GetSecretValue(req);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error(std::string("[secrets] Failed to fetch '") + secret_name +
                                     "': " + outcome.GetError().GetMessage().c_str());
        }
        json_str = outcome.GetResult().GetSecretString().c_str();
        ygg::log::info("[secrets] Fetched secret '{}'", secret_name);
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
