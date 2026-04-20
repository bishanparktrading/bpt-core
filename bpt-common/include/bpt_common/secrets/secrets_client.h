#pragma once

// Fetch a secret (set of key-value pairs) delivered by systemd-creds.
//
// PRODUCTION / QA
// ---------------
// Expects the service's systemd unit to declare:
//   LoadCredentialEncrypted=<name>:/etc/bpt/creds/<name>.cred
// systemd decrypts the .cred at service start, writes plaintext to a
// per-service tmpfs mount, and sets $CREDENTIALS_DIRECTORY. This function
// reads $CREDENTIALS_DIRECTORY/<name> and parses it as KEY=value lines.
//
// With env == QA or PROD, $CREDENTIALS_DIRECTORY MUST be set — a missing
// LoadCredentialEncrypted= fails loudly here rather than silently reading
// a stale or attacker-planted file from $HOME. QA is deliberately as
// strict as PROD so a misconfigured unit can't slip qa → prod unnoticed.
//
// DEV FALLBACK
// ------------
// With env == DEV, when $CREDENTIALS_DIRECTORY is unset, the loader
// falls back to a directory on disk. Default: $HOME/.bpt-secrets/.
// Override via $BPT_DEV_SECRETS_DIR — useful for per-checkout secret
// sets, CI, or containers that don't want to depend on $HOME.
//
// Format — one per line, `#` for comments, blank lines ignored:
//   OKX_API_KEY=...
//   OKX_SECRET=...
//   OKX_PASSPHRASE=...
//
// NAME NORMALIZATION
// ------------------
// Forward slashes in `secret_name` are rewritten to hyphens for the filename
// lookup (systemd-creds names can't contain slashes). So a config value of
// "bpt/testnet/OKX" resolves to $CREDENTIALS_DIRECTORY/bpt-testnet-OKX.

#include <bpt_common/env.h>
#include <map>
#include <string>

namespace bpt::common::secrets {

// env determines strict vs permissive delivery — see file header.
std::map<std::string, std::string> fetch(const std::string& secret_name, Env env);

}  // namespace bpt::common::secrets
