// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n()
#include <cctype>
#include <chrono>
#include <cstdio> /* fprintf() */
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>

#include <event2/http.h> /* for HTTP_OK */

#include <fmt/core.h>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer-common.h"
#include "benc.h"
#include "crypto-utils.h"
#include "error.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h" /* pex */
#include "quark.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
#include "web-utils.h"
#include "web.h"

using namespace std::literals;

namespace
{
void verboseLog(std::string_view description, tr_direction direction, std::string_view message)
{
    auto& out = std::cerr;
    static bool const verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    if (!verbose)
    {
        return;
    }

    auto const direction_sv = direction == TR_DOWN ? "<< "sv : ">> "sv;
    out << description << std::endl << "[raw]"sv << direction_sv;
    for (unsigned char const ch : message)
    {
        if (isprint(ch) != 0)
        {
            out << ch;
        }
        else
        {
            out << R"(\x)" << std::hex << std::setw(2) << std::setfill('0') << unsigned(ch) << std::dec << std::setw(1)
                << std::setfill(' ');
        }
    }
    out << std::endl << "[b64]"sv << direction_sv << tr_base64_encode(message) << std::endl;
}

auto constexpr MaxBencDepth = 8;
} // namespace

// --- Announce

namespace
{
namespace announce_helpers
{
[[nodiscard]] constexpr std::string_view get_event_string(tr_announce_request const& req)
{
    return req.partial_seed && (req.event != TR_ANNOUNCE_EVENT_STOPPED) ? "paused"sv : tr_announce_event_get_string(req.event);
}

struct http_announce_data
{
    http_announce_data(tr_sha1_digest_t info_hash_in, tr_announce_response_func on_response_in, std::string_view log_name_in)
        : info_hash{ info_hash_in }
        , on_response{ std::move(on_response_in) }
        , log_name{ log_name_in }
    {
    }

    tr_sha1_digest_t info_hash = {};
    std::optional<tr_announce_response> previous_response;

    tr_announce_response_func on_response;
    bool http_success = false;

    uint8_t requests_sent_count = {};
    uint8_t requests_answered_count = {};

    std::string log_name;
};

bool handleAnnounceResponse(tr_web::FetchResponse const& web_response, tr_announce_response* const response)
{
    auto const& [status, body, did_connect, did_timeout, vdata] = web_response;
    auto const& log_name = static_cast<http_announce_data const*>(vdata)->log_name;

    response->did_connect = did_connect;
    response->did_timeout = did_timeout;
    tr_logAddTrace("Got announce response", log_name);

    if (status != HTTP_OK)
    {
        auto const* const response_str = tr_webGetResponseStr(status);
        response->errmsg = fmt::format(FMT_STRING("Tracker HTTP response {:d} ({:s})"), status, response_str);

        return false;
    }

    tr_announcerParseHttpAnnounceResponse(*response, body, log_name);

    if (!std::empty(response->pex6))
    {
        tr_logAddTrace(fmt::format("got a peers6 length of {}", std::size(response->pex6)), log_name);
    }

    if (!std::empty(response->pex))
    {
        tr_logAddTrace(fmt::format("got a peers length of {}", std::size(response->pex)), log_name);
    }

    return true;
}

void onAnnounceDone(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vdata] = web_response;
    auto* data = static_cast<struct http_announce_data*>(vdata);

    ++data->requests_answered_count;

    // If another request already succeeded (or we don't have a registered callback),
    // skip processing this response:
    if (!data->http_success && data->on_response)
    {
        tr_announce_response response;
        response.info_hash = data->info_hash;

        data->http_success = handleAnnounceResponse(web_response, &response);

        if (data->http_success)
        {
            data->on_response(response);
        }
        else if (data->requests_answered_count == data->requests_sent_count)
        {
            auto const* response_used = &response;

            // All requests have been answered, but none were successful.
            // Choose the one that went further to report.
            if (data->previous_response && !response.did_connect && !response.did_timeout)
            {
                response_used = &*data->previous_response;
            }

            data->on_response(*response_used);
        }
        else
        {
            // There is still one request pending that might succeed, so store
            // the response for later. There is only room for 1 previous response,
            // because there can be at most 2 requests.
            TR_ASSERT(!data->previous_response);
            data->previous_response = std::move(response);
        }
    }
    else
    {
        tr_logAddTrace("Ignoring redundant announce response", data->log_name);
    }

    // Free data if no more responses are expected:
    if (data->requests_answered_count == data->requests_sent_count)
    {
        delete data;
    }
}

void announce_url_new(tr_urlbuf& url, tr_session const* session, tr_announce_request const& req)
{
    url.clear();
    auto out = std::back_inserter(url);

    auto escaped_info_hash = tr_urlbuf{};
    tr_urlPercentEncode(std::back_inserter(escaped_info_hash), req.info_hash);

    fmt::format_to(
        out,
        "{url}"
        "{sep}info_hash={info_hash}"
        "&peer_id={peer_id}"
        "&port={port}"
        "&uploaded={uploaded}"
        "&downloaded={downloaded}"
        "&left={left}"
        "&numwant={numwant}"
        "&key={key}"
        "&compact=1"
        "&supportcrypto=1",
        fmt::arg("url", req.announce_url),
        fmt::arg("sep", tr_strvContains(req.announce_url.sv(), '?') ? '&' : '?'),
        fmt::arg("info_hash", std::data(escaped_info_hash)),
        fmt::arg("peer_id", std::string_view{ std::data(req.peer_id), std::size(req.peer_id) }),
        fmt::arg("port", req.port.host()),
        fmt::arg("uploaded", req.up),
        fmt::arg("downloaded", req.down),
        fmt::arg("left", req.leftUntilComplete),
        fmt::arg("numwant", req.numwant),
        fmt::arg("key", req.key));

    if (session->encryptionMode() == TR_ENCRYPTION_REQUIRED)
    {
        fmt::format_to(out, "&requirecrypto=1");
    }

    if (req.corrupt != 0)
    {
        fmt::format_to(out, "&corrupt={}", req.corrupt);
    }

    if (auto const str = get_event_string(req); !std::empty(str))
    {
        fmt::format_to(out, "&event={}", str);
    }

    if (!std::empty(req.tracker_id))
    {
        fmt::format_to(out, "&trackerid={}", req.tracker_id);
    }
}

[[nodiscard]] std::string format_ip_arg(std::string_view ip)
{
    return fmt::format("&ip={:s}", ip);
}

} // namespace announce_helpers
} // namespace

void tr_tracker_http_announce(
    tr_session const* session,
    tr_announce_request const& request,
    tr_announce_response_func on_response)
{
    using namespace announce_helpers;

    auto* const d = new http_announce_data{ request.info_hash, std::move(on_response), request.log_name };

    /* There are two alternative techniques for announcing both IPv4 and
       IPv6 addresses. Previous version of BEP-7 suggests adding "ipv4="
       and "ipv6=" parameters to the announce URL, while OpenTracker and
       newer version of BEP-7 requires that peers announce once per each
       public address they want to use.

       We should ensure that we send the announce both via IPv6 and IPv4,
       and to be safe we also add the "ipv6=" and "ipv4=" parameters, if
       we already have them. Our global IPv6 address is computed for the
       LTEP handshake, so this comes for free. Our public IPv4 address
       may have been returned from a previous announce and stored in the
       session.
     */
    auto url = tr_urlbuf{};
    announce_url_new(url, session, request);
    auto options = tr_web::FetchOptions{ url.sv(), onAnnounceDone, d };
    options.timeout_secs = TR_ANNOUNCE_TIMEOUT_SEC;
    options.sndbuf = 4096;
    options.rcvbuf = 4096;

    auto do_make_request = [&](std::string_view const& protocol_name, tr_web::FetchOptions&& opt)
    {
        tr_logAddTrace(fmt::format("Sending {} announce to libcurl: '{}'", protocol_name, opt.url), request.log_name);
        session->fetch(std::move(opt));
    };

    auto const [ipv6, ipv6_is_any] = session->publicAddress(TR_AF_INET6);

    /*
     * Before Curl 7.77.0, if we explicitly choose the IP version we want
     * to use, it is still possible that the wrong one is used. The workaround
     * is expensive (disabling DNS cache), so instead we have to make do with
     * a request that we don't know if will go through IPv6 or IPv4.
     */
    static auto const use_curl_workaround = curl_version_info(CURLVERSION_NOW)->version_num < 0x074D00 /* 7.77.0 */;
    if (use_curl_workaround)
    {
        if (session->useAnnounceIP())
        {
            options.url += format_ip_arg(session->announceIP());
        }

        d->requests_sent_count = 1;
        do_make_request(""sv, std::move(options));
    }
    else
    {
        if (session->useAnnounceIP() || ipv6_is_any)
        {
            if (session->useAnnounceIP())
            {
                options.url += format_ip_arg(session->announceIP());
            }
            d->requests_sent_count = 1;
            do_make_request(""sv, std::move(options));
        }
        else
        {
            d->requests_sent_count = 2;

            // First try to send the announce via IPv4:
            auto ipv4_options = options;
            ipv4_options.ip_proto = tr_web::FetchOptions::IPProtocol::V4;
            do_make_request("IPv4"sv, std::move(ipv4_options));

            // Then try to send via IPv6:
            options.ip_proto = tr_web::FetchOptions::IPProtocol::V6;
            do_make_request("IPv6"sv, std::move(options));
        }
    }
}

void tr_announcerParseHttpAnnounceResponse(tr_announce_response& response, std::string_view benc, std::string_view log_name)
{
    verboseLog("Announce response:", TR_DOWN, benc);

    struct AnnounceHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
    {
        using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

        tr_announce_response& response_;
        std::string_view const log_name_;
        std::optional<size_t> row_;
        tr_pex pex_ = {};

        explicit AnnounceHandler(tr_announce_response& response, std::string_view log_name)
            : response_{ response }
            , log_name_{ log_name }
        {
        }

        bool StartDict(Context const& context) override
        {
            BasicHandler::StartDict(context);

            pex_ = {};

            return true;
        }

        bool EndDict(Context const& context) override
        {
            BasicHandler::EndDict(context);

            if (pex_.is_valid_for_peers())
            {
                response_.pex.push_back(pex_);
                pex_ = {};
            }

            return true;
        }

        bool Int64(int64_t value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); key == "interval")
            {
                response_.interval = static_cast<int>(value);
            }
            else if (key == "min interval"sv)
            {
                response_.min_interval = static_cast<int>(value);
            }
            else if (key == "complete"sv)
            {
                response_.seeders = static_cast<int>(value);
            }
            else if (key == "incomplete"sv)
            {
                response_.leechers = static_cast<int>(value);
            }
            else if (key == "downloaded"sv)
            {
                response_.downloads = static_cast<int>(value);
            }
            else if (key == "port"sv)
            {
                pex_.port.setHost(static_cast<uint16_t>(value));
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }

        bool String(std::string_view value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); key == "failure reason"sv)
            {
                response_.errmsg = value;
            }
            else if (key == "warning message"sv)
            {
                response_.warning = value;
            }
            else if (key == "tracker id"sv)
            {
                response_.tracker_id = value;
            }
            else if (key == "peers"sv)
            {
                response_.pex = tr_pex::from_compact_ipv4(std::data(value), std::size(value), nullptr, 0);
            }
            else if (key == "peers6"sv)
            {
                response_.pex6 = tr_pex::from_compact_ipv6(std::data(value), std::size(value), nullptr, 0);
            }
            else if (key == "ip")
            {
                if (auto const addr = tr_address::from_string(value); addr)
                {
                    pex_.addr = *addr;
                }
            }
            else if (key == "peer id")
            {
                // unused
            }
            else if (key == "external ip"sv && std::size(value) == 4)
            {
                auto const [addr, out] = tr_address::from_compact_ipv4(reinterpret_cast<std::byte const*>(std::data(value)));
                response_.external_ip = addr;
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }
    };

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = AnnounceHandler{ response, log_name };
    tr_error* error = nullptr;
    transmission::benc::parse(benc, stack, handler, nullptr, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(
            fmt::format(
                _("Couldn't parse announce response: {error} ({error_code})"),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)),
            log_name);
        tr_error_clear(&error);
    }
}

// ---

namespace
{
namespace scrape_helpers
{
class scrape_data
{
public:
    scrape_data(tr_scrape_response_func response_func, std::string_view log_name)
        : response_func_{ std::move(response_func) }
        , log_name_{ log_name }
    {
    }

    [[nodiscard]] constexpr auto& response() noexcept
    {
        return response_;
    }

    [[nodiscard]] constexpr auto const& log_name() const noexcept
    {
        return log_name_;
    }

    void invoke_callback() const
    {
        if (response_func_)
        {
            response_func_(response_);
        }
    }

private:
    tr_scrape_response response_ = {};
    tr_scrape_response_func response_func_ = {};
    std::string log_name_;
};

void onScrapeDone(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vdata] = web_response;
    auto* const data = static_cast<scrape_data*>(vdata);

    auto& response = data->response();
    response.did_connect = did_connect;
    response.did_timeout = did_timeout;

    auto const scrape_url_sv = response.scrape_url.sv();
    tr_logAddTrace(fmt::format("Got scrape response for '{}'", scrape_url_sv), data->log_name());

    if (status != HTTP_OK)
    {
        auto const* const response_str = tr_webGetResponseStr(status);
        response.errmsg = fmt::format(FMT_STRING("Tracker HTTP response {:d} ({:s})"), status, response_str);
    }
    else if (!std::empty(body))
    {
        tr_announcerParseHttpScrapeResponse(response, body, data->log_name());
    }

    data->invoke_callback();
    delete data;
}

void scrape_url_new(tr_pathbuf& scrape_url, tr_scrape_request const& req)
{
    scrape_url = req.scrape_url.sv();
    char delimiter = tr_strvContains(scrape_url, '?') ? '&' : '?';

    for (int i = 0; i < req.info_hash_count; ++i)
    {
        scrape_url.append(delimiter, "info_hash=");
        tr_urlPercentEncode(std::back_inserter(scrape_url), req.info_hash[i]);
        delimiter = '&';
    }
}
} // namespace scrape_helpers
} // namespace

void tr_tracker_http_scrape(tr_session const* session, tr_scrape_request const& request, tr_scrape_response_func on_response)
{
    using namespace scrape_helpers;

    auto* d = new scrape_data{ std::move(on_response), request.log_name };

    auto& response = d->response();
    response.scrape_url = request.scrape_url;
    response.row_count = request.info_hash_count;
    for (int i = 0; i < response.row_count; ++i)
    {
        response.rows[i].info_hash = request.info_hash[i];
        response.rows[i].seeders = -1;
        response.rows[i].leechers = -1;
        response.rows[i].downloads = -1;
    }

    auto scrape_url = tr_pathbuf{};
    scrape_url_new(scrape_url, request);
    tr_logAddTrace(fmt::format("Sending scrape to libcurl: '{}'", scrape_url), request.log_name);
    auto options = tr_web::FetchOptions{ scrape_url, onScrapeDone, d };
    options.timeout_secs = TR_SCRAPE_TIMEOUT_SEC;
    options.sndbuf = 4096;
    options.rcvbuf = 4096;
    session->fetch(std::move(options));
}

void tr_announcerParseHttpScrapeResponse(tr_scrape_response& response, std::string_view benc, std::string_view log_name)
{
    verboseLog("Scrape response:", TR_DOWN, benc);

    struct ScrapeHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
    {
        using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

        tr_scrape_response& response_;
        std::string_view const log_name_;
        std::optional<size_t> row_;

        explicit ScrapeHandler(tr_scrape_response& response, std::string_view const log_name)
            : response_{ response }
            , log_name_{ log_name }
        {
        }

        bool Key(std::string_view value, Context const& context) override
        {
            BasicHandler::Key(value, context);

            if (auto needle = tr_sha1_digest_t{}; depth() == 2 && key(1) == "files"sv && std::size(value) == std::size(needle))
            {
                std::copy_n(reinterpret_cast<std::byte const*>(std::data(value)), std::size(value), std::data(needle));
                auto const it = std::find_if(
                    std::begin(response_.rows),
                    std::end(response_.rows),
                    [needle](auto const& row) { return row.info_hash == needle; });

                if (it == std::end(response_.rows))
                {
                    row_.reset();
                }
                else
                {
                    row_ = std::distance(std::begin(response_.rows), it);
                }
            }

            return true;
        }

        bool Int64(int64_t value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); row_ && key == "complete"sv)
            {
                response_.rows[*row_].seeders = static_cast<int>(value);
            }
            else if (row_ && key == "downloaded"sv)
            {
                response_.rows[*row_].downloads = static_cast<int>(value);
            }
            else if (row_ && key == "incomplete"sv)
            {
                response_.rows[*row_].leechers = static_cast<int>(value);
            }
            else if (row_ && key == "downloaders"sv)
            {
                response_.rows[*row_].downloaders = static_cast<int>(value);
            }
            else if (key == "min_request_interval"sv)
            {
                response_.min_request_interval = static_cast<int>(value);
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }

        bool String(std::string_view value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); depth() == 1 && key == "failure reason"sv)
            {
                response_.errmsg = value;
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' str '{}'", key, value), log_name_);
            }

            return true;
        }
    };

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = ScrapeHandler{ response, log_name };
    tr_error* error = nullptr;
    transmission::benc::parse(benc, stack, handler, nullptr, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(
            fmt::format(
                _("Couldn't parse scrape response: {error} ({error_code})"),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)),
            log_name);
        tr_error_clear(&error);
    }
}
