#include "yggdrasil/secrets/secrets_client.h"

#include "yggdrasil/logging.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ygg::secrets {

std::map<std::string, std::string> fetch(const std::string& secret_name) {
    std::string name = secret_name;
    std::replace(name.begin(), name.end(), '/', '-');

    std::filesystem::path path;
    if (const char* dir = std::getenv("CREDENTIALS_DIRECTORY")) {
        path = std::filesystem::path(dir) / name;
    } else if (const char* home = std::getenv("HOME")) {
        path = std::filesystem::path(home) / ".bpt-secrets" / name;
    } else {
        throw std::runtime_error(
            "[secrets] neither CREDENTIALS_DIRECTORY nor HOME is set — "
            "is this running under systemd with LoadCredentialEncrypted=?");
    }

    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("[secrets] cannot open " + path.string());
    }

    std::map<std::string, std::string> out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }

    ygg::log::info("[secrets] Loaded '{}' ({} keys)", name, out.size());
    return out;
}

}  // namespace ygg::secrets
