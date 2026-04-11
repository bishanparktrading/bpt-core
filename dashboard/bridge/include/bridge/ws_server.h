#pragma once

#include <boost/asio.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace bridge {

class WsSession;

// Minimal broadcast-style WebSocket server.  One io_context on its own thread,
// accepting clients on a single port.  Messages are fan-out broadcast to every
// connected session.
//
// Usage:
//   WsServer server(8080);
//   server.start();                 // non-blocking
//   server.broadcast(R"({"type":"tick",...})");
//   server.stop();
class WsServer {
public:
    explicit WsServer(uint16_t port);
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    // Start the io_context on a background thread and begin accepting.
    void start();

    // Stop accepting, close all sessions, join the IO thread.
    void stop();

    // Broadcast a message to every connected client.  Thread-safe; callable
    // from any thread (the Aeron polling thread in practice).
    void broadcast(std::string message);

    // Called by sessions on their own thread.  Internal, but needs to be
    // public so sessions can register/deregister.
    void add_session(std::shared_ptr<WsSession> session);
    void remove_session(const std::shared_ptr<WsSession>& session);

private:
    void do_accept();

    uint16_t port_;
    boost::asio::io_context io_ctx_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread io_thread_;

    std::mutex sessions_mutex_;
    std::unordered_set<std::shared_ptr<WsSession>> sessions_;
};

}  // namespace bridge
