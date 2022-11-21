// This file Copyright © 2009-2022 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib> // for abort()
#include <cstring> // for memcpy()
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple> // for std::tie()

#ifdef _WIN32
#include <ws2tcpip.h>
#undef gai_strerror
#define gai_strerror gai_strerrorA
#else
#include <sys/time.h> // for `struct timezone`
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netdb.h>
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h" // for tr_peerMgrCompactToPex()
#include "timer.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-strbuf.h"
#include "variant.h"
#include "utils.h" // for tr_time(), _()

using namespace std::literals;

// the dht library needs us to implement these:
extern "C"
{

    // This function should return true when a node is blacklisted.
    // We don't support using a blacklist with the DHT in Transmission,
    // since massive (ab)use of this feature could harm the DHT. However,
    // feel free to add support to your private copy as long as you don't
    // redistribute it.
    int dht_blacklisted(sockaddr const* /*sa*/, int /*salen*/)
    {
        return 0;
    }

    void dht_hash(
        void* hash_return,
        int hash_size,
        void const* v1,
        int len1,
        void const* v2,
        int len2,
        void const* v3,
        int len3)
    {
        auto* setme = reinterpret_cast<std::byte*>(hash_return);
        std::fill_n(static_cast<char*>(hash_return), hash_size, '\0');

        auto const sv1 = std::string_view{ static_cast<char const*>(v1), size_t(len1) };
        auto const sv2 = std::string_view{ static_cast<char const*>(v2), size_t(len2) };
        auto const sv3 = std::string_view{ static_cast<char const*>(v3), size_t(len3) };
        auto const digest = tr_sha1::digest(sv1, sv2, sv3);
        std::copy_n(std::data(digest), std::min(size_t(hash_size), std::size(digest)), setme);
    }

    int dht_random_bytes(void* buf, size_t size)
    {
        if (!tr_rand_buffer(buf, size))
        {
            return -1;
        }
        return static_cast<int>(size);
    }

    int dht_sendto(int sockfd, void const* buf, int len, int flags, struct sockaddr const* to, int tolen)
    {
        return static_cast<int>(sendto(sockfd, static_cast<char const*>(buf), len, flags, to, tolen));
    }

#if defined(_WIN32) && !defined(__MINGW32__)
    int dht_gettimeofday(struct timeval* tv, [[maybe_unused]] struct timezone* tz)
    {
        TR_ASSERT(tz == nullptr);

        auto const d = std::chrono::system_clock::now().time_since_epoch();
        auto const s = std::chrono::duration_cast<std::chrono::seconds>(d);
        tv->tv_sec = s.count();
        tv->tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(d - s).count();

        return 0;
    }
#endif

} // extern "C"

class tr_dht_impl final : public tr_dht
{
private:
    using Node = std::pair<tr_address, tr_port>;
    using Nodes = std::deque<Node>;
    using Id = std::array<unsigned char, 20>;

    enum class SwarmStatus
    {
        Stopped,
        Broken,
        Poor,
        Firewalled,
        Good
    };

public:
    tr_dht_impl(Mediator& mediator, tr_port peer_port, tr_socket_t udp4_socket, tr_socket_t udp6_socket)
        : peer_port_{ peer_port }
        , udp4_socket_{ udp4_socket }
        , udp6_socket_{ udp6_socket }
        , mediator_{ mediator }
        , state_filename_{ tr_pathbuf{ mediator_.configDir(), "/dht.dat" } }
        , announce_timer_{ mediator_.timerMaker().create([this]() { onAnnounceTimer(); }) }
        , bootstrap_timer_{ mediator_.timerMaker().create([this]() { onBootstrapTimer(); }) }
        , periodic_timer_{ mediator_.timerMaker().create([this]() { onPeriodicTimer(); }) }
    {
        tr_logAddDebug(fmt::format("Starting DHT on port {port}", fmt::arg("port", peer_port.host())));

        // load up the bootstrap nodes
        if (tr_sys_path_exists(state_filename_.c_str()))
        {
            std::tie(id_, bootstrap_queue_) = loadState(state_filename_);
        }
        getNodesFromBootstrapFile(tr_pathbuf{ mediator_.configDir(), "/dht.bootstrap"sv }, bootstrap_queue_);
        getNodesFromName("dht.transmissionbt.com", tr_port::fromHost(6881), bootstrap_queue_);
        bootstrap_timer_->startSingleShot(100ms);

        mediator_.api().init(udp4_socket_, udp6_socket_, std::data(id_), nullptr);

        onAnnounceTimer();
        announce_timer_->startRepeating(1s);

        onPeriodicTimer();
    }

    tr_dht_impl(tr_dht_impl&&) = delete;
    tr_dht_impl(tr_dht_impl const&) = delete;
    tr_dht_impl& operator=(tr_dht_impl&&) = delete;
    tr_dht_impl& operator=(tr_dht_impl const&) = delete;

    ~tr_dht_impl() override
    {
        tr_logAddTrace("Uninitializing DHT");

        // Since we only save known good nodes,
        // only overwrite older data if we know enough nodes.
        if (isReady(AF_INET) || isReady(AF_INET6))
        {
            saveState();
        }

        mediator_.api().uninit();
        tr_logAddTrace("Done uninitializing DHT");
    }

    void addNode(tr_address const& addr, tr_port port) override
    {
        if (addr.isIPv4())
        {
            auto sin = sockaddr_in{};
            sin.sin_family = AF_INET;
            sin.sin_addr = addr.addr.addr4;
            sin.sin_port = port.network();
            mediator_.api().ping_node((struct sockaddr*)&sin, sizeof(sin));
        }
        else if (addr.isIPv6())
        {
            auto sin6 = sockaddr_in6{};
            sin6.sin6_family = AF_INET6;
            sin6.sin6_addr = addr.addr.addr6;
            sin6.sin6_port = port.network();
            mediator_.api().ping_node((struct sockaddr*)&sin6, sizeof(sin6));
        }
    }

    void handleMessage(unsigned char const* msg, size_t msglen, struct sockaddr* from, socklen_t fromlen) override
    {
        auto const call_again_in_n_secs = periodic(msg, msglen, from, fromlen);

        // Being slightly late is fine,
        // and has the added benefit of adding some jitter.
        auto const interval = call_again_in_n_secs + std::chrono::milliseconds{ tr_rand_int_weak(1000) };
        periodic_timer_->startSingleShot(interval);
    }

private:
    [[nodiscard]] constexpr tr_socket_t udpSocket(int af) const noexcept
    {
        switch (af)
        {
        case AF_INET:
            return udp4_socket_;

        case AF_INET6:
            return udp6_socket_;

        default:
            return TR_BAD_SOCKET;
        }
    }

    [[nodiscard]] SwarmStatus swarmStatus(int family, int* const setme_node_count = nullptr) const
    {
        if (udpSocket(family) == TR_BAD_SOCKET)
        {
            if (setme_node_count != nullptr)
            {
                *setme_node_count = 0;
            }

            return SwarmStatus::Stopped;
        }

        int good = 0;
        int dubious = 0;
        int incoming = 0;
        mediator_.api().nodes(family, &good, &dubious, nullptr, &incoming);

        if (setme_node_count != nullptr)
        {
            *setme_node_count = good + dubious;
        }

        if (good < 4 || good + dubious <= 8)
        {
            return SwarmStatus::Broken;
        }

        if (good < 40)
        {
            return SwarmStatus::Poor;
        }

        if (incoming < 8)
        {
            return SwarmStatus::Firewalled;
        }

        return SwarmStatus::Good;
    }

    [[nodiscard]] static constexpr auto isReady(SwarmStatus const status)
    {
        return status >= SwarmStatus::Firewalled;
    }

    [[nodiscard]] bool isReady(int af) const noexcept
    {
        return isReady(swarmStatus(af));
    }

    [[nodiscard]] bool isReady() const noexcept
    {
        return isReady(AF_INET) && isReady(AF_INET6);
    }

    ///

    // how long to wait between adding nodes during bootstrap
    [[nodiscard]] static constexpr auto bootstrapInterval(size_t n_added)
    {
        // Our DHT code is able to take up to 9 nodes in a row without
        // dropping any. After that, it takes some time to split buckets.
        // So ping the first 8 nodes quickly, then slow down.
        if (n_added < 8U)
        {
            return 2s;
        }

        if (n_added < 16U)
        {
            return 15s;
        }

        return 40s;
    }

    void onBootstrapTimer()
    {
        // Since we don't want to abuse our bootstrap nodes,
        // we don't ping them if the DHT is in a good state.
        if (isReady() || std::empty(bootstrap_queue_))
        {
            return;
        }

        auto [address, port] = bootstrap_queue_.front();
        bootstrap_queue_.pop_front();
        addNode(address, port);
        ++n_bootstrapped_;

        bootstrap_timer_->startSingleShot(bootstrapInterval(n_bootstrapped_));
    }

    ///

    [[nodiscard]] auto announceTorrent(tr_sha1_digest_t const& info_hash, int af, tr_port port)
    {
        auto const* dht_hash = reinterpret_cast<unsigned char const*>(std::data(info_hash));
        auto const rc = mediator_.api().search(dht_hash, port.host(), af, callback, this);
        auto const announce_again_in_n_secs = rc < 0 ? 5s + std::chrono::seconds{ tr_rand_int_weak(5) } :
                                                       25min + std::chrono::seconds{ tr_rand_int_weak(3 * 60) };
        return announce_again_in_n_secs;
    }

    void onAnnounceTimer()
    {
        // don't announce if the swarm isn't ready
        if (swarmStatus(AF_INET) < SwarmStatus::Poor && swarmStatus(AF_INET6) < SwarmStatus::Poor)
        {
            return;
        }

        auto const now = tr_time();
        for (auto const id : mediator_.torrentsAllowingDHT())
        {
            auto& times = announce_times_[id];

            if (auto& announce_after = times.ipv4_announce_after; announce_after < now)
            {
                auto const announce_again_in_n_secs = announceTorrent(mediator_.torrentInfoHash(id), AF_INET, peer_port_);
                announce_after = now + std::chrono::seconds{ announce_again_in_n_secs }.count();
            }

            if (auto& announce_after = times.ipv6_announce_after; announce_after < now)
            {
                auto const announce_again_in_n_secs = announceTorrent(mediator_.torrentInfoHash(id), AF_INET6, peer_port_);
                announce_after = now + std::chrono::seconds{ announce_again_in_n_secs }.count();
            }
        }
    }

    ///

    void onPeriodicTimer()
    {
        auto const call_again_in_n_secs = periodic(nullptr, 0, nullptr, 0);

        // Being slightly late is fine,
        // and has the added benefit of adding some jitter.
        auto const interval = call_again_in_n_secs + std::chrono::milliseconds{ tr_rand_int_weak(1000) };
        periodic_timer_->startSingleShot(interval);
    }

    [[nodiscard]] std::chrono::seconds periodic(
        unsigned char const* msg,
        size_t msglen,
        struct sockaddr const* from,
        socklen_t fromlen)
    {
        TR_ASSERT_MSG(msglen == 0 || msg[msglen] == '\0', "libdht requires zero-terminated msg");

        auto call_again_in_n_secs = time_t{};
        mediator_.api().periodic(msg, msglen, from, static_cast<int>(fromlen), &call_again_in_n_secs, callback, this);
        return std::chrono::seconds{ call_again_in_n_secs };
    }

    static void callback(void* vself, int event, unsigned char const* info_hash, void const* data, size_t data_len)
    {
        auto* const self = static_cast<tr_dht_impl*>(vself);
        auto hash = tr_sha1_digest_t{};
        std::copy_n(reinterpret_cast<std::byte const*>(info_hash), std::size(hash), std::data(hash));

        if (event == DHT_EVENT_VALUES)
        {
            auto const pex = tr_pex::fromCompact4(data, data_len, nullptr, 0);
            self->mediator_.addPex(hash, std::data(pex), std::size(pex));
        }
        else if (event == DHT_EVENT_VALUES6)
        {
            auto const pex = tr_pex::fromCompact6(data, data_len, nullptr, 0);
            self->mediator_.addPex(hash, std::data(pex), std::size(pex));
        }
    }

    ///

    void saveState() const
    {
        auto constexpr MaxNodes = int{ 300 };
        auto constexpr PortLen = size_t{ 2 };
        auto constexpr CompactAddrLen = size_t{ 4 };
        auto constexpr CompactLen = size_t{ CompactAddrLen + PortLen };
        auto constexpr Compact6AddrLen = size_t{ 16 };
        auto constexpr Compact6Len = size_t{ Compact6AddrLen + PortLen };

        auto sins4 = std::array<struct sockaddr_in, MaxNodes>{};
        auto sins6 = std::array<struct sockaddr_in6, MaxNodes>{};
        auto num4 = int{ MaxNodes };
        auto num6 = int{ MaxNodes };
        auto const n = mediator_.api().get_nodes(std::data(sins4), &num4, std::data(sins6), &num6);
        tr_logAddTrace(fmt::format("Saving {} ({} + {}) nodes", n, num4, num6));

        tr_variant benc;
        tr_variantInitDict(&benc, 3);
        tr_variantDictAddRaw(&benc, TR_KEY_id, std::data(id_), std::size(id_));

        if (num4 > 0)
        {
            auto compact = std::array<char, MaxNodes * CompactLen>{};
            char* out = std::data(compact);
            for (auto const* in = std::data(sins4), *end = in + num4; in != end; ++in)
            {
                memcpy(out, &in->sin_addr, CompactAddrLen);
                out += CompactAddrLen;
                memcpy(out, &in->sin_port, PortLen); // saved in network byte order
                out += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes, std::data(compact), out - std::data(compact));
        }

        if (num6 > 0)
        {
            auto compact6 = std::array<char, MaxNodes * Compact6Len>{};
            char* out6 = std::data(compact6);
            for (auto const* in = std::data(sins6), *end = in + num6; in != end; ++in)
            {
                memcpy(out6, &in->sin6_addr, Compact6AddrLen);
                out6 += Compact6AddrLen;
                memcpy(out6, &in->sin6_port, PortLen); // saved in network byte order
                out6 += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes6, std::data(compact6), out6 - std::data(compact6));
        }

        tr_variantToFile(&benc, TR_VARIANT_FMT_BENC, state_filename_);
        tr_variantClear(&benc);
    }

    [[nodiscard]] static std::pair<Id, Nodes> loadState(std::string_view filename)
    {
        // Note that DHT ids need to be distributed uniformly,
        // so it should be something truly random
        auto id = Id{};
        tr_rand_buffer(std::data(id), std::size(id));

        auto nodes = Nodes{};

        if (auto dict = tr_variant{}; tr_variantFromFile(&dict, TR_VARIANT_PARSE_BENC, filename))
        {
            if (auto sv = std::string_view{};
                tr_variantDictFindStrView(&dict, TR_KEY_id, &sv) && std::size(sv) == std::size(id))
            {
                std::copy(std::begin(sv), std::end(sv), std::begin(id));
            }

            size_t raw_len = 0U;
            std::byte const* raw = nullptr;
            if (tr_variantDictFindRaw(&dict, TR_KEY_nodes, &raw, &raw_len) && raw_len % 6 == 0)
            {
                auto* walk = raw;
                auto const* const end = raw + raw_len;
                while (walk < end)
                {
                    auto addr = tr_address{};
                    auto port = tr_port{};
                    std::tie(addr, walk) = tr_address::fromCompact4(walk);
                    std::tie(port, walk) = tr_port::fromCompact(walk);
                    nodes.emplace_back(addr, port);
                }
            }

            if (tr_variantDictFindRaw(&dict, TR_KEY_nodes6, &raw, &raw_len) && raw_len % 18 == 0)
            {
                auto* walk = raw;
                auto const* const end = raw + raw_len;
                while (walk < end)
                {
                    auto addr = tr_address{};
                    auto port = tr_port{};
                    std::tie(addr, walk) = tr_address::fromCompact6(walk);
                    std::tie(port, walk) = tr_port::fromCompact(walk);
                    nodes.emplace_back(addr, port);
                }
            }

            tr_variantClear(&dict);
        }

        return std::make_pair(id, nodes);
    }

    ///

    static void getNodesFromBootstrapFile(std::string_view filename, Nodes& nodes)
    {
        auto in = std::ifstream{ std::string{ filename } };
        if (!in.is_open())
        {
            return;
        }

        // format is each line has host, a space char, and port number
        auto line = std::string{};
        while (std::getline(in, line))
        {
            auto line_stream = std::istringstream{ line };
            auto addrstr = std::string{};
            auto hport = uint16_t{};
            line_stream >> addrstr >> hport;

            if (line_stream.bad() || std::empty(addrstr))
            {
                tr_logAddWarn(fmt::format(
                    _("Couldn't parse '{filename}' line: '{line}'"),
                    fmt::arg("filename", filename),
                    fmt::arg("line", line)));
            }
            else
            {
                getNodesFromName(addrstr.c_str(), tr_port::fromHost(hport), nodes);
            }
        }
    }

    static void getNodesFromName(char const* name, tr_port port_in, Nodes& nodes)
    {
        auto hints = addrinfo{};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_protocol = 0;
        hints.ai_flags = 0;

        auto port_str = std::array<char, 16>{};
        *fmt::format_to(std::data(port_str), FMT_STRING("{:d}"), port_in.host()) = '\0';

        addrinfo* info = nullptr;
        if (int const rc = getaddrinfo(name, std::data(port_str), &hints, &info); rc != 0)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't look up '{address}:{port}': {error} ({error_code})"),
                fmt::arg("address", name),
                fmt::arg("port", port_in.host()),
                fmt::arg("error", gai_strerror(rc)),
                fmt::arg("error_code", rc)));
            return;
        }

        for (auto* infop = info; infop != nullptr; infop = infop->ai_next)
        {
            if (auto addrport = tr_address::fromSockaddr(infop->ai_addr); addrport)
            {
                nodes.emplace_back(addrport->first, addrport->second);
            }
        }

        freeaddrinfo(info);
    }

    ///

    tr_port const peer_port_;
    tr_socket_t const udp4_socket_;
    tr_socket_t const udp6_socket_;

    Mediator& mediator_;
    std::string const state_filename_;
    std::unique_ptr<libtransmission::Timer> const announce_timer_;
    std::unique_ptr<libtransmission::Timer> const bootstrap_timer_;
    std::unique_ptr<libtransmission::Timer> const periodic_timer_;

    Id id_ = {};

    Nodes bootstrap_queue_;
    size_t n_bootstrapped_ = 0;

    struct AnnounceInfo
    {
        time_t ipv4_announce_after = 0;
        time_t ipv6_announce_after = 0;
    };

    std::map<tr_torrent_id_t, AnnounceInfo> announce_times_;
};

[[nodiscard]] std::unique_ptr<tr_dht> tr_dht::create(
    Mediator& mediator,
    tr_port peer_port,
    tr_socket_t udp4_socket,
    tr_socket_t udp6_socket)
{
    return std::make_unique<tr_dht_impl>(mediator, peer_port, udp4_socket, udp6_socket);
}
