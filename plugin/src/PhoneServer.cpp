#include <winsock2.h>
#include <ws2tcpip.h>

#include "PhoneServer.h"

#include <juce_core/juce_core.h>
#include "BinaryData.h"

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <algorithm>
#include <map>
#include <vector>

//==============================================================================
struct PhoneServer::Tls
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    std::mutex rngLock;

    Tls()
    {
        mbedtls_entropy_init (&entropy);
        mbedtls_ctr_drbg_init (&drbg);
        mbedtls_ssl_config_init (&conf);
        mbedtls_x509_crt_init (&cert);
        mbedtls_pk_init (&key);
    }

    ~Tls()
    {
        mbedtls_ssl_config_free (&conf);
        mbedtls_x509_crt_free (&cert);
        mbedtls_pk_free (&key);
        mbedtls_ctr_drbg_free (&drbg);
        mbedtls_entropy_free (&entropy);
    }

    // The DRBG is shared by every connection thread and isn't thread-safe on its own.
    static int rng (void* self, unsigned char* out, size_t len)
    {
        auto* t = static_cast<Tls*> (self);
        const std::lock_guard<std::mutex> lg (t->rngLock);
        return mbedtls_ctr_drbg_random (&t->drbg, out, len);
    }

    bool init (std::string& error)
    {
        psa_crypto_init();

        static const unsigned char seedName[] = "MobiMic";

        if (mbedtls_ctr_drbg_seed (&drbg, mbedtls_entropy_func, &entropy, seedName, sizeof (seedName)) != 0)
        {
            error = "Could not initialise the random generator";
            return false;
        }

        if (! loadCertificate() && ! (createCertificate() && loadCertificate()))
        {
            error = "Could not create the security certificate";
            return false;
        }

        if (mbedtls_ssl_config_defaults (&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT) != 0
            || mbedtls_ssl_conf_own_cert (&conf, &cert, &key) != 0)
        {
            error = "Could not set up the secure server";
            return false;
        }

        mbedtls_ssl_conf_rng (&conf, rng, this);
        mbedtls_ssl_conf_max_tls_version (&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_read_timeout (&conf, 1000); // lets blocked reads notice shutdown
        return true;
    }

    static juce::File getFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MobiMic");
    }

    bool loadCertificate()
    {
        const auto certFile = getFolder().getChildFile ("cert.pem");
        const auto keyFile = getFolder().getChildFile ("key.pem");

        if (! certFile.existsAsFile() || ! keyFile.existsAsFile())
            return false;

        // PEM parsing wants the terminating NUL included in the length.
        const auto certPem = certFile.loadFileAsString().toStdString();
        const auto keyPem = keyFile.loadFileAsString().toStdString();

        mbedtls_x509_crt_free (&cert);
        mbedtls_x509_crt_init (&cert);
        mbedtls_pk_free (&key);
        mbedtls_pk_init (&key);

        if (mbedtls_x509_crt_parse (&cert, (const unsigned char*) certPem.c_str(), certPem.size() + 1) != 0)
            return false;

        if (mbedtls_pk_parse_key (&key, (const unsigned char*) keyPem.c_str(), keyPem.size() + 1,
                                  nullptr, 0, rng, this) != 0)
            return false;

        return cert.valid_to.year > juce::Time::getCurrentTime().getYear();
    }

    bool createCertificate()
    {
        mbedtls_pk_context newKey;
        mbedtls_x509write_cert crt;
        mbedtls_pk_init (&newKey);
        mbedtls_x509write_crt_init (&crt);

        std::vector<unsigned char> certPem (4096), keyPem (2048);
        unsigned char serial[16];

        const auto now = juce::Time::getCurrentTime();
        const auto notBefore = (now - juce::RelativeTime::days (2)).formatted ("%Y%m%d%H%M%S").toStdString();
        const auto notAfter = std::to_string (now.getYear() + 10) + "0101000000";

        bool ok = mbedtls_pk_setup (&newKey, mbedtls_pk_info_from_type (MBEDTLS_PK_ECKEY)) == 0
               && mbedtls_ecp_gen_key (MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec (newKey), rng, this) == 0
               && rng (this, serial, sizeof (serial)) == 0;

        if (ok)
        {
            serial[0] = (unsigned char) ((serial[0] & 0x7f) | 0x01); // positive, non-zero

            mbedtls_x509write_crt_set_subject_key (&crt, &newKey);
            mbedtls_x509write_crt_set_issuer_key (&crt, &newKey);
            mbedtls_x509write_crt_set_version (&crt, MBEDTLS_X509_CRT_VERSION_3);
            mbedtls_x509write_crt_set_md_alg (&crt, MBEDTLS_MD_SHA256);

            ok = mbedtls_x509write_crt_set_subject_name (&crt, "CN=MobiMic") == 0
              && mbedtls_x509write_crt_set_issuer_name (&crt, "CN=MobiMic") == 0
              && mbedtls_x509write_crt_set_serial_raw (&crt, serial, sizeof (serial)) == 0
              && mbedtls_x509write_crt_set_validity (&crt, notBefore.c_str(), notAfter.c_str()) == 0
              && mbedtls_x509write_crt_set_basic_constraints (&crt, 0, -1) == 0
              && mbedtls_x509write_crt_pem (&crt, certPem.data(), certPem.size(), rng, this) == 0
              && mbedtls_pk_write_key_pem (&newKey, keyPem.data(), keyPem.size()) == 0;
        }

        if (ok)
        {
            const auto folder = getFolder();
            folder.createDirectory();
            ok = folder.getChildFile ("cert.pem").replaceWithText (juce::String ((const char*) certPem.data()))
              && folder.getChildFile ("key.pem").replaceWithText (juce::String ((const char*) keyPem.data()));
        }

        mbedtls_x509write_crt_free (&crt);
        mbedtls_pk_free (&newKey);
        return ok;
    }
};

//==============================================================================
struct PhoneServer::Client
{
    std::thread thread;
    std::atomic<bool> done { false };
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    uint64_t id = 0;
};

namespace
{
    constexpr size_t maxMessageBytes = 1 << 20;
    constexpr int maxClients = 16;

    bool writeAll (mbedtls_ssl_context& ssl, const void* data, size_t len)
    {
        auto* p = static_cast<const unsigned char*> (data);

        while (len > 0)
        {
            const int r = mbedtls_ssl_write (&ssl, p, len);

            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;

            if (r <= 0)
                return false;

            p += r;
            len -= (size_t) r;
        }

        return true;
    }

    /** Reads at least one byte; gives up after `idleSeconds` of silence or on shutdown. */
    int readSome (mbedtls_ssl_context& ssl, unsigned char* dest, size_t max,
                  int idleSeconds, const std::atomic<bool>& stopping)
    {
        int idle = 0;

        for (;;)
        {
            const int r = mbedtls_ssl_read (&ssl, dest, max);

            if (r > 0)
                return r;

            if (r == MBEDTLS_ERR_SSL_TIMEOUT)
            {
                if (++idle >= idleSeconds || stopping.load())
                    return -1;

                continue;
            }

            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;

            return -1;
        }
    }

    bool readExact (mbedtls_ssl_context& ssl, unsigned char* dest, size_t len,
                    int idleSeconds, const std::atomic<bool>& stopping)
    {
        while (len > 0)
        {
            const int r = readSome (ssl, dest, len, idleSeconds, stopping);

            if (r <= 0)
                return false;

            dest += r;
            len -= (size_t) r;
        }

        return true;
    }

    bool sendFrame (mbedtls_ssl_context& ssl, int opcode, const void* payload, size_t len)
    {
        unsigned char header[4];
        size_t headerLen = 2;
        header[0] = (unsigned char) (0x80 | opcode);

        if (len < 126)
        {
            header[1] = (unsigned char) len;
        }
        else
        {
            header[1] = 126;
            header[2] = (unsigned char) (len >> 8);
            header[3] = (unsigned char) (len & 0xff);
            headerLen = 4;
        }

        return writeAll (ssl, header, headerLen) && (len == 0 || writeAll (ssl, payload, len));
    }

    bool sendHttp (mbedtls_ssl_context& ssl, const char* status, const char* contentType,
                   const char* body, size_t bodyLen)
    {
        const auto head = std::string ("HTTP/1.1 ") + status + "\r\n"
                        + "Content-Type: " + contentType + "\r\n"
                        + "Content-Length: " + std::to_string (bodyLen) + "\r\n"
                        + "Cache-Control: no-store\r\n"
                        + "Connection: close\r\n\r\n";

        return writeAll (ssl, head.data(), head.size()) && (bodyLen == 0 || writeAll (ssl, body, bodyLen));
    }

    std::string toLower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return s;
    }

    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t");
        const auto b = s.find_last_not_of (" \t");
        return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
    }
}

//==============================================================================
PhoneServer::PhoneServer() = default;

PhoneServer::~PhoneServer()
{
    stop();
}

bool PhoneServer::start (std::string& error)
{
    const std::lock_guard<std::mutex> lg (startLock);

    if (running.load())
        return true;

    WSADATA wsa;
    WSAStartup (MAKEWORD (2, 2), &wsa);

    if (tls == nullptr)
    {
        auto t = std::make_unique<Tls>();

        if (! t->init (error))
            return false;

        tls = std::move (t);
    }

    for (int p = firstPort; p <= lastPort; ++p)
    {
        const SOCKET s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (s == INVALID_SOCKET)
            break;

        // Without this, Windows lets two programs bind the same port and we'd never notice the clash.
        const BOOL exclusive = TRUE;
        setsockopt (s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*) &exclusive, sizeof (exclusive));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_ANY);
        addr.sin_port = htons ((u_short) p);

        if (bind (s, (const sockaddr*) &addr, sizeof (addr)) == 0 && listen (s, 8) == 0)
        {
            listenSocket = (std::intptr_t) s;
            port = p;
            stopping = false;
            acceptThread = std::thread ([this] { acceptLoop(); });
            running = true;
            return true;
        }

        closesocket (s);
    }

    error = "Ports " + std::to_string (firstPort) + "-" + std::to_string (lastPort) + " are all in use";
    return false;
}

void PhoneServer::stop()
{
    const std::lock_guard<std::mutex> lg (startLock);

    if (! running.load())
        return;

    stopping = true;

    if (acceptThread.joinable())
        acceptThread.join();

    closesocket ((SOCKET) listenSocket);
    listenSocket = -1;
    running = false;
    phoneConnected = false;
    port = 0;
}

bool PhoneServer::claim (Listener* l)
{
    const std::lock_guard<std::mutex> lg (listenerLock);

    if (listener == nullptr)
        listener = l;

    return listener == l;
}

void PhoneServer::release (Listener* l)
{
    const std::lock_guard<std::mutex> lg (listenerLock);

    if (listener == l)
        listener = nullptr;
}

void PhoneServer::acceptLoop()
{
    const auto listenFd = (SOCKET) listenSocket;

    while (! stopping.load())
    {
        clients.remove_if ([] (std::unique_ptr<Client>& c)
        {
            if (! c->done.load())
                return false;

            c->thread.join();
            return true;
        });

        fd_set readSet;
        FD_ZERO (&readSet);
        FD_SET (listenFd, &readSet);
        timeval timeout { 0, 250000 };

        if (select (0, &readSet, nullptr, nullptr, &timeout) <= 0)
            continue;

        const SOCKET s = accept (listenFd, nullptr, nullptr);

        if (s == INVALID_SOCKET)
            continue;

        if ((int) clients.size() >= maxClients)
        {
            closesocket (s);
            continue;
        }

        const BOOL noDelay = TRUE;
        setsockopt (s, IPPROTO_TCP, TCP_NODELAY, (const char*) &noDelay, sizeof (noDelay));

        auto client = std::make_unique<Client>();
        auto* c = client.get();
        mbedtls_net_init (&c->net);
        c->net.fd = (int) s;
        c->id = ++nextSocketId;
        c->thread = std::thread ([this, c]
        {
            serveClient (*c);
            c->done = true;
        });
        clients.push_back (std::move (client));
    }

    for (auto& c : clients)
        c->thread.join();

    clients.clear();
}

void PhoneServer::serveClient (Client& c)
{
    auto& ssl = c.ssl;
    mbedtls_ssl_init (&ssl);

    const auto finish = [&]
    {
        mbedtls_ssl_close_notify (&ssl);
        mbedtls_ssl_free (&ssl);
        mbedtls_net_free (&c.net);
    };

    if (mbedtls_ssl_setup (&ssl, &tls->conf) != 0)
        return finish();

    mbedtls_ssl_set_bio (&ssl, &c.net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

    // Browsers open spare connections and abandon ones that fail the certificate check, so be patient but bounded.
    for (int waited = 0;;)
    {
        const int r = mbedtls_ssl_handshake (&ssl);

        if (r == 0)
            break;

        if (r == MBEDTLS_ERR_SSL_TIMEOUT && ++waited < 10 && ! stopping.load())
            continue;

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;

        return finish();
    }

    // Read the HTTP request head.
    std::string request;
    unsigned char chunk[1024];

    while (request.find ("\r\n\r\n") == std::string::npos)
    {
        const int r = readSome (ssl, chunk, sizeof (chunk), 10, stopping);

        if (r <= 0 || request.size() > 8192)
            return finish();

        request.append ((const char*) chunk, (size_t) r);
    }

    const auto firstLineEnd = request.find ("\r\n");
    const auto firstLine = request.substr (0, firstLineEnd);
    const auto sp1 = firstLine.find (' ');
    const auto sp2 = firstLine.find (' ', sp1 + 1);

    if (sp1 == std::string::npos || sp2 == std::string::npos)
        return finish();

    const auto method = firstLine.substr (0, sp1);
    auto path = firstLine.substr (sp1 + 1, sp2 - sp1 - 1);
    path = path.substr (0, path.find ('?'));

    std::map<std::string, std::string> headers;

    for (auto pos = firstLineEnd + 2;;)
    {
        const auto end = request.find ("\r\n", pos);

        if (end == std::string::npos || end == pos)
            break;

        const auto line = request.substr (pos, end - pos);
        const auto colon = line.find (':');

        if (colon != std::string::npos)
            headers[toLower (trim (line.substr (0, colon)))] = trim (line.substr (colon + 1));

        pos = end + 2;
    }

    if (method != "GET")
    {
        sendHttp (ssl, "405 Method Not Allowed", "text/plain", "", 0);
    }
    else if (path == "/ws" && toLower (headers["upgrade"]) == "websocket" && ! headers["sec-websocket-key"].empty())
    {
        serveWebSocket (c, headers["sec-websocket-key"]);
    }
    else if (path == "/" || path == "/index.html")
    {
        sendHttp (ssl, "200 OK", "text/html; charset=utf-8", BinaryData::index_html, (size_t) BinaryData::index_htmlSize);
    }
    else
    {
        sendHttp (ssl, "404 Not Found", "text/plain", "", 0);
    }

    finish();
}

void PhoneServer::serveWebSocket (Client& c, const std::string& key)
{
    auto& ssl = c.ssl;

    // Handshake: prove we understood the request by hashing its key with the protocol's fixed GUID.
    const auto accept = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha[20];
    unsigned char b64[64];
    size_t b64Len = 0;
    mbedtls_sha1 ((const unsigned char*) accept.data(), accept.size(), sha);
    mbedtls_base64_encode (b64, sizeof (b64), &b64Len, sha, sizeof (sha));

    const auto response = std::string ("HTTP/1.1 101 Switching Protocols\r\n"
                                       "Upgrade: websocket\r\n"
                                       "Connection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: ")
                        + std::string ((const char*) b64, b64Len) + "\r\n\r\n";

    if (! writeAll (ssl, response.data(), response.size()))
        return;

    // One phone at a time: the newest connection wins, the previous one notices and closes.
    activeSocketId = c.id;
    phoneConnected = true;

    {
        const std::lock_guard<std::mutex> lg (listenerLock);

        if (listener != nullptr)
            listener->phoneConnectionChanged (true);
    }

    std::vector<unsigned char> message, payload;
    int messageOpcode = 0;
    constexpr int idleSeconds = 5; // the page pings every second, so silence means the phone is gone

    while (! stopping.load() && activeSocketId.load() == c.id)
    {
        unsigned char head[2];

        if (! readExact (ssl, head, 2, idleSeconds, stopping))
            break;

        const bool fin = (head[0] & 0x80) != 0;
        const int opcode = head[0] & 0x0f;
        const bool masked = (head[1] & 0x80) != 0;
        uint64_t len = head[1] & 0x7f;

        if (len == 126)
        {
            unsigned char ext[2];

            if (! readExact (ssl, ext, 2, idleSeconds, stopping))
                break;

            len = ((uint64_t) ext[0] << 8) | ext[1];
        }
        else if (len == 127)
        {
            unsigned char ext[8];

            if (! readExact (ssl, ext, 8, idleSeconds, stopping))
                break;

            len = 0;

            for (auto b : ext)
                len = (len << 8) | b;
        }

        unsigned char mask[4] = {};

        if (! masked || len > maxMessageBytes || ! readExact (ssl, mask, 4, idleSeconds, stopping))
            break;

        payload.resize ((size_t) len);

        if (len > 0 && ! readExact (ssl, payload.data(), payload.size(), idleSeconds, stopping))
            break;

        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] ^= mask[i & 3];

        if (opcode == 0x8) // close
        {
            sendFrame (ssl, 0x8, payload.data(), std::min<size_t> (payload.size(), 2));
            break;
        }

        if (opcode == 0x9) // ping
        {
            if (! sendFrame (ssl, 0xA, payload.data(), std::min<size_t> (payload.size(), 125)))
                break;

            continue;
        }

        if (opcode == 0xA) // pong
            continue;

        if (opcode != 0)
        {
            messageOpcode = opcode;
            message.clear();
        }

        if (message.size() + payload.size() > maxMessageBytes)
            break;

        message.insert (message.end(), payload.begin(), payload.end());

        if (! fin)
            continue;

        if (messageOpcode == 0x2) // binary: little-endian int16 mono at 48 kHz
        {
            const std::lock_guard<std::mutex> lg (listenerLock);

            if (listener != nullptr && message.size() >= 2)
                listener->phonePcm (reinterpret_cast<const int16_t*> (message.data()), (int) (message.size() / 2));
        }
        else if (messageOpcode == 0x1 && message.size() == 4 && std::equal (message.begin(), message.end(), "ping"))
        {
            std::string stats = "{}";

            {
                const std::lock_guard<std::mutex> lg (listenerLock);

                if (listener != nullptr)
                    stats = listener->phoneStatsJson();
            }

            if (! sendFrame (ssl, 0x1, stats.data(), stats.size()))
                break;
        }
    }

    auto expected = c.id;

    if (activeSocketId.compare_exchange_strong (expected, 0))
    {
        phoneConnected = false;

        const std::lock_guard<std::mutex> lg (listenerLock);

        if (listener != nullptr)
            listener->phoneConnectionChanged (false);
    }
}
