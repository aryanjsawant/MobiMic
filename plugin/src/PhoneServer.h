#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

/** The tiny HTTPS + WebSocket server the phone's browser talks to.

    Browsers only allow microphone access on secure pages, so this serves the
    phone page over TLS with a self-signed certificate (created on first run)
    and receives the audio over a secure WebSocket on the same port.

    One instance is shared by every plugin instance in the host process; the
    single Listener is whichever plugin instance currently owns the phone.
*/
class PhoneServer
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        /** Called on a network thread with 48 kHz mono samples. */
        virtual void phonePcm (const int16_t* samples, int numSamples) = 0;
        virtual void phoneConnectionChanged (bool connected) = 0;
        /** JSON sent back to the phone once a second. */
        virtual std::string phoneStatsJson() = 0;
    };

    PhoneServer();
    ~PhoneServer();

    /** Starts listening if not already. Returns false and fills `error` on failure. */
    bool start (std::string& error);
    void stop();

    bool isRunning() const          { return running.load(); }
    int getPort() const             { return port.load(); }
    bool isPhoneConnected() const   { return phoneConnected.load(); }

    /** The first caller becomes the owner; returns true if `l` is (now) the owner. */
    bool claim (Listener* l);
    void release (Listener* l);

    static constexpr int firstPort = 8443, lastPort = 8452;

private:
    struct Tls;
    struct Client;

    void acceptLoop();
    void serveClient (Client&);
    void serveWebSocket (Client&, const std::string& key);

    std::unique_ptr<Tls> tls;
    std::mutex startLock;
    std::atomic<bool> running { false }, stopping { false }, phoneConnected { false };
    std::atomic<int> port { 0 };
    std::atomic<uint64_t> activeSocketId { 0 };
    uint64_t nextSocketId = 0;
    std::intptr_t listenSocket = -1;
    std::thread acceptThread;
    std::list<std::unique_ptr<Client>> clients; // accept thread only

    std::mutex listenerLock;
    Listener* listener = nullptr;
};
