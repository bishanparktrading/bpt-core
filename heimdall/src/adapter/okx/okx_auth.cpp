#include "heimdall/adapter/okx/okx_auth.h"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/json.hpp>
#include <chrono>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <string>

namespace heimdall::adapter::okx {

namespace http = boost::beast::http;
namespace json = boost::json;

namespace {
uint64_t epoch_seconds_now() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
}  // namespace

std::string base64_encode(const unsigned char* data, std::size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);

    BUF_MEM* buf;
    BIO_get_mem_ptr(b64, &buf);
    std::string out(buf->data, buf->length);
    BIO_free_all(b64);
    return out;
}

std::string hmac_sha256_b64(std::string_view key, std::string_view data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(),
         static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         static_cast<int>(data.size()),
         digest,
         &digest_len);
    return base64_encode(digest, digest_len);
}

std::string build_login_msg(std::string_view api_key,
                             std::string_view secret_key,
                             std::string_view passphrase) {
    const std::string ts_str = std::to_string(epoch_seconds_now());
    const std::string prehash = ts_str + "GET" + "/users/self/verify";
    const std::string sign = hmac_sha256_b64(secret_key, prehash);

    json::object arg;
    arg["apiKey"] = std::string(api_key);
    arg["passphrase"] = std::string(passphrase);
    arg["timestamp"] = ts_str;
    arg["sign"] = sign;

    json::object msg;
    msg["op"] = "login";
    msg["args"] = json::array{std::move(arg)};
    return json::serialize(msg);
}

void sign_get_request(http::request<http::string_body>& req,
                      std::string_view host,
                      std::string_view path,
                      std::string_view api_key,
                      std::string_view secret_key,
                      std::string_view passphrase,
                      bool testnet) {
    const std::string ts_str = std::to_string(epoch_seconds_now());
    const std::string prehash = ts_str + "GET" + std::string(path);
    const std::string sign = hmac_sha256_b64(secret_key, prehash);

    req.set(http::field::host, std::string(host));
    req.set(http::field::user_agent, "heimdall/0.1");
    req.set("OK-ACCESS-KEY", std::string(api_key));
    req.set("OK-ACCESS-SIGN", sign);
    req.set("OK-ACCESS-TIMESTAMP", ts_str);
    req.set("OK-ACCESS-PASSPHRASE", std::string(passphrase));
    if (testnet)
        req.set("x-simulated-trading", "1");
}

}  // namespace heimdall::adapter::okx
