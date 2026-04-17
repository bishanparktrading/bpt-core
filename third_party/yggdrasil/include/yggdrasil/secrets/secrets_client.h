#pragma once

// Fetch a secret (set of key-value pairs) delivered by systemd-creds.
//
// PRODUCTION
// ----------
// Expects the service's systemd unit to declare:
//   LoadCredentialEncrypted=<name>:/etc/bpt/creds/<name>.cred
// systemd decrypts the .cred at service start, writes plaintext to a
// per-service tmpfs mount, and sets $CREDENTIALS_DIRECTORY. This function
// reads $CREDENTIALS_DIRECTORY/<name> and parses it as KEY=value lines.
//
// Format — one per line, `#` for comments, blank lines ignored:
//   OKX_API_KEY=...
//   OKX_SECRET=...
//   OKX_PASSPHRASE=...
//
// DEV FALLBACK
// ------------
// When $CREDENTIALS_DIRECTORY is unset (e.g. running outside systemd), reads
// from ~/.bpt-secrets/<name>. Same KEY=value format.
//
// NAME NORMALIZATION
// ------------------
// Forward slashes in `secret_name` are rewritten to hyphens for the filename
// lookup (systemd-creds names can't contain slashes). So a config value of
// "bpt/testnet/OKX" resolves to $CREDENTIALS_DIRECTORY/bpt-testnet-OKX.

#include <map>
#include <string>

namespace ygg::secrets {

std::map<std::string, std::string> fetch(const std::string& secret_name);

}  // namespace ygg::secrets
