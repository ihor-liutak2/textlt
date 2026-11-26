#include "textlt/connect/irc_provider.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <tuple>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>

#include "protocol.hpp"

namespace textlt::connect {
namespace {

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using asio::ip::tcp;
using Error = boost::system::error_code;
using namespace std::chrono_literals;
using namespace irc_detail;

bool SameProfile(const IrcProfile& a, const IrcProfile& b) {
    const auto fields = [](const IrcProfile& p) {
        return std::tie(p.id, p.name, p.server, p.port, p.tls, p.nickname, p.username,
                        p.sasl_account, p.sasl_password, p.server_password,
                        p.channels, p.auto_reconnect);
    };
    return fields(a) == fields(b);
}

bool OffersSaslPlain(std::string_view capabilities) {
    while (!capabilities.empty()) {
        const auto end = capabilities.find(' ');
        const auto capability = capabilities.substr(0, end);
        if (capability == "sasl") return true;
        if (capability.substr(0, 5) == "sasl=") {
            auto mechanisms = capability.substr(5);
            while (!mechanisms.empty()) {
                const auto comma = mechanisms.find(',');
                if (mechanisms.substr(0, comma) == "PLAIN") return true;
                if (comma == std::string_view::npos) break;
                mechanisms.remove_prefix(comma + 1);
            }
        }
        if (end == std::string_view::npos) break;
        capabilities.remove_prefix(end + 1);
    }
    return false;
}

struct Write {
    std::string bytes;
    std::function<void(bool)> done;
};

struct Wire {
    explicit Wire(asio::io_context& io)
        : resolver(io), context(ssl::context::tls_client), stream(io, context), timer(io) {}

    tcp::resolver resolver;
    ssl::context context;
    ssl::stream<tcp::socket> stream;
    asio::steady_timer timer;
    asio::streambuf input{9216};
    std::deque<Write> writes;
    bool writing = false;
    bool registered = false;
    bool closing = false;
    bool sent_ping = false;
    enum class Registration { Listing, RequestingSasl, Challenge, Authenticating, Ended };
    Registration registration = Registration::Listing;
    bool offers_sasl_plain = false;
    std::string registration_notice;
    std::set<std::string> capabilities;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    using Report = std::function<void(IrcStatus)>;

    Session(asio::io_context& io, IrcProfile profile, ProviderEventSink emit,
            Report report, IrcProvider::TestResult test = {})
        : profile(std::move(profile)), io_(io), retry_(io), emit_(std::move(emit)),
          report_(std::move(report)), test_(std::move(test)), nickname_(this->profile.nickname),
          wanted_(this->profile.channels) {}

    const IrcProfile profile;

    void Start() {
        if (enabled_) return;
        CloseWire();
        enabled_ = true;
        failures_ = 0;
        Connect();
    }

    void Stop(std::string status = "Stopped") {
        enabled_ = false;
        retry_.cancel();
        const auto wire = wire_;
        if (!wire || !wire->registered || !wire->stream.lowest_layer().is_open()) {
            CloseWire();
        } else if (!wire->closing) {
            wire->closing = true;
            while (wire->writes.size() > static_cast<std::size_t>(wire->writing)) {
                auto done = std::move(wire->writes.back().done);
                wire->writes.pop_back();
                if (done) done(false);
            }
            Arm(wire, 750ms, "Stopped");
            Queue(wire, "QUIT :TextLT closing\r\n", [self = shared_from_this(), wire](bool) {
                if (self->wire_ == wire) self->CloseWire();
            });
        }
        if (test_) FinishTest("IRC connection test cancelled.");
        else Publish({false, false, std::move(status)});
    }

    void Join(std::string target) {
        if (!Online()) return;
        const auto matches = [&](const std::string& item) { return SameTarget(item, target); };
        if (std::none_of(wanted_.begin(), wanted_.end(), matches)) wanted_.push_back(target);
        EnsurePeer(target, true);
        Queue(wire_, "JOIN " + target + "\r\n");
    }

    void Part(std::string target) {
        const auto key = Fold(target, mapping_);
        wanted_.erase(std::remove_if(wanted_.begin(), wanted_.end(),
            [&](const std::string& item) { return Fold(item, mapping_) == key; }), wanted_.end());
        const auto peer = peers_.find(key);
        if (peer != peers_.end() && Online()) {
            Queue(wire_, "PART " + peer->second.target + "\r\n");
        } else if (peer != peers_.end()) {
            PeerEvent(key, peer->second);
        }
    }

    void Open(std::string target) { EnsurePeer(target, false); }

    void Send(const TransferIdentity& identity, std::string_view route, std::string text) {
        const auto peer = peers_.find(std::string(route));
        if (!Online() || peer == peers_.end() || (peer->second.channel && !peer->second.joined)) {
            Transfer(identity, false, "IRC conversation is offline or the channel has not been joined.");
            return;
        }

        auto lines = Privmsg(peer->second.target, text);
        if (lines.empty()) {
            Transfer(identity, false, "IRC messages must contain valid UTF-8 text.");
            return;
        }
        std::string bytes;
        for (auto& line : lines) bytes += line;
        Queue(wire_, std::move(bytes), [self = shared_from_this(), identity](bool ok) {
            self->Transfer(identity, ok, ok ? "" : "IRC disconnected before sending the message.");
        });
    }

private:
    struct Target {
        std::string target;
        bool channel;
        bool joined = false;
    };

    bool Online() const { return enabled_ && wire_ && wire_->registered && !wire_->closing; }

    bool SameTarget(std::string_view a, std::string_view b) const {
        return Fold(a, mapping_) == Fold(b, mapping_);
    }

    void Publish(IrcStatus status) {
        if (report_) report_(status);
        for (auto& [key, peer] : peers_) {
            if (!status.online) peer.joined = false;
            PeerEvent(key, peer);
        }
    }

    void PeerEvent(const std::string& key, const Target& peer) {
        if (emit_) emit_(Peer{{kIrcProviderId, profile.id + "/" + key},
            profile.name + " / " + peer.target, Online() && (!peer.channel || peer.joined),
            {{kTextTypeId, {}}}});
    }

    Target& EnsurePeer(const std::string& target, bool channel) {
        const auto key = Fold(target, mapping_);
        const auto [it, added] = peers_.try_emplace(key, Target{target, channel});
        if (added) PeerEvent(key, it->second);
        return it->second;
    }

    void Transfer(const TransferIdentity& identity, bool ok, std::string error) {
        if (emit_) emit_(TransferEvent{identity, TransferDirection::Outgoing,
            ok ? TransferState::Completed : TransferState::Failed, {}, {},
            ok ? std::nullopt : std::make_optional(std::move(error)),
            ok ? std::nullopt : std::make_optional(TransferFailure::ConnectionFailure)});
    }

    void Connect() {
        std::shared_ptr<Wire> wire;
        Error ec;
        try {
            wire = std::make_shared<Wire>(io_);
            if (profile.tls) {
                wire->context.set_default_verify_paths(ec);
#ifdef __APPLE__
                // Packaged OpenSSL can have build-machine default certificate paths.
                if (!std::getenv("SSL_CERT_FILE")) {
                    Error system_ca_error;
                    wire->context.load_verify_file("/etc/ssl/cert.pem", system_ca_error);
                    if (!system_ca_error) ec.clear();
                }
#endif
                if (ec) {
                    Fail("Could not load trusted TLS certificates.", true);
                    return;
                }
                wire->stream.set_verify_mode(ssl::verify_peer);
                Error address_error;
                asio::ip::make_address(profile.server, address_error);
                auto* tls = wire->stream.native_handle();
                const bool identity_set = address_error
                    ? SSL_set_tlsext_host_name(tls, profile.server.c_str()) == 1 &&
                        SSL_set1_host(tls, profile.server.c_str()) == 1
                    : X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(tls), profile.server.c_str()) == 1;
                if (!identity_set) {
                    Fail("Could not set the IRC TLS server name.", true);
                    return;
                }
            }
        } catch (const std::exception&) {
            Fail("Could not initialize the IRC TLS transport.", true);
            return;
        }
        wire_ = wire;
        nickname_ = profile.nickname;
        Publish({true, false, "Resolving " + profile.server});
        Arm(wire, 15s, "IRC connection or registration timed out.");
        auto self = shared_from_this();
        wire->resolver.async_resolve(profile.server, std::to_string(profile.port),
            [self, wire](const Error& error, tcp::resolver::results_type endpoints) {
                if (wire != self->wire_) return;
                if (error) return self->Fail("Could not resolve the IRC server.");
                self->Publish({true, false, "Connecting to IRC server"});
                asio::async_connect(wire->stream.lowest_layer(), endpoints,
                    [self, wire](const Error& connect_error, const tcp::endpoint&) {
                        if (wire != self->wire_) return;
                        if (connect_error) return self->Fail("Could not connect to the IRC server.");
                        if (!self->profile.tls) return self->Register(wire);
                        self->Publish({true, false, "Verifying TLS certificate"});
                        wire->stream.async_handshake(ssl::stream_base::client,
                            [self, wire](const Error& tls_error) {
                                if (wire != self->wire_) return;
                                if (tls_error) {
                                    const bool invalid_cert =
                                        SSL_get_verify_result(wire->stream.native_handle()) != X509_V_OK;
                                    return self->Fail(invalid_cert ? "IRC TLS certificate validation failed."
                                        : "IRC TLS handshake failed.", invalid_cert);
                                }
                                self->Register(wire);
                            });
                    });
            });
    }

    void Register(const std::shared_ptr<Wire>& wire) {
        Publish({true, false, "Registering IRC nickname"});
        if (!profile.server_password.empty()) Queue(wire, "PASS :" + profile.server_password + "\r\n");
        Queue(wire, "CAP LS 302\r\n");
        Queue(wire, "NICK " + profile.nickname + "\r\n");
        Queue(wire, "USER " + profile.username + " 0 * :" + profile.username + "\r\n");
        Read(wire);
    }

    void Arm(const std::shared_ptr<Wire>& wire, std::chrono::milliseconds delay, std::string message) {
        wire->timer.expires_after(delay);
        wire->timer.async_wait([self = shared_from_this(), wire, message = std::move(message)](const Error& ec) {
            if (ec || wire != self->wire_) return;
            if (wire->closing) return self->CloseWire();
            if (!wire->registered || wire->sent_ping) return self->Fail(self->WithNotice(*wire, message));
            wire->sent_ping = true;
            self->Queue(wire, "PING :TextLT\r\n");
            self->Arm(wire, 30s, "IRC server stopped responding.");
        });
    }

    void Read(const std::shared_ptr<Wire>& wire) {
        const auto done = [self = shared_from_this(), wire](const Error& ec, std::size_t) {
            if (wire != self->wire_ || wire->closing) return;
            if (ec) {
                if (ec == asio::error::not_found) return self->Fail("An IRC server line exceeded the limit.");
                return self->Fail(self->WithNotice(*wire, "IRC connection closed."));
            }
            std::istream in(&wire->input);
            std::string line;
            std::getline(in, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (wire->registered) {
                wire->sent_ping = false;
                self->Arm(wire, 90s, "IRC server stopped responding.");
            }
            if (line.find('\0') == std::string::npos) self->Handle(wire, Parse(line));
            if (wire == self->wire_ && !wire->closing) self->Read(wire);
        };
        if (profile.tls) asio::async_read_until(wire->stream, wire->input, '\n', done);
        else asio::async_read_until(wire->stream.next_layer(), wire->input, '\n', done);
    }

    void Queue(const std::shared_ptr<Wire>& wire, std::string bytes, std::function<void(bool)> done = {}) {
        wire->writes.push_back({std::move(bytes), std::move(done)});
        if (!wire->writing) WriteNext(wire);
    }

    void WriteNext(const std::shared_ptr<Wire>& wire) {
        wire->writing = !wire->writes.empty();
        if (!wire->writing) return;
        const auto done = [self = shared_from_this(), wire](const Error& ec, std::size_t) {
            if (wire != self->wire_) return;
            if (ec) {
                if (wire->closing) return self->CloseWire();
                return self->Fail(self->WithNotice(*wire, "Could not write to the IRC server."));
            }
            auto complete = std::move(wire->writes.front().done);
            wire->writes.pop_front();
            if (complete) complete(true);
            if (wire == self->wire_) self->WriteNext(wire);
        };
        const auto bytes = asio::buffer(wire->writes.front().bytes);
        if (profile.tls) asio::async_write(wire->stream, bytes, done);
        else asio::async_write(wire->stream.next_layer(), bytes, done);
    }

    void Handle(const std::shared_ptr<Wire>& wire, const Message& message) {
        const auto& p = message.parameters;
        const auto command = message.command;
        const auto nick = message.prefix.substr(0, message.prefix.find_first_of("!@"));
        if (command == "PING" && !p.empty()) {
            std::string pong = "PONG ";
            if (p.size() > 1 && Token(p.front())) pong += std::string(p.front()) + " ";
            pong += ":" + std::string(p.back()) + "\r\n";
            if (pong.size() <= 512 && p.back().find('\r') == std::string_view::npos) Queue(wire, std::move(pong));
        } else if (command == "NOTICE" && p.size() >= 2) {
            const auto notice = ServerText(p.back());
            if (notice.empty()) return;
            if (!wire->registered) wire->registration_notice = notice;
            if (report_) report_({true, wire->registered, "IRC notice: " + notice});
        } else if (command == "ERROR") {
            const auto reason = p.empty() ? std::string{} : ServerText(p.back());
            Fail(reason.empty() ? WithNotice(*wire, "IRC server closed the connection.")
                                : "IRC server error: " + reason,
                 !wire->registered && RequiresSasl(reason + " " + wire->registration_notice));
        } else if (!wire->registered) {
            RegisterReply(wire, message);
        } else if (command == "005") {
            for (const auto entry : p) {
                constexpr std::string_view key = "CASEMAPPING=";
                if (entry.substr(0, key.size()) != key || !peers_.empty()) continue;
                const auto mapping = entry.substr(key.size());
                if (mapping == "ascii" || mapping == "strict-rfc1459" || mapping == "rfc1459") mapping_ = mapping;
            }
        } else if (command == "CAP" && p.size() >= 3) {
            const auto update = [&](std::string_view list, bool enabled) {
                while (!list.empty()) {
                    const auto end = list.find(' ');
                    auto capability = list.substr(0, end);
                    const auto equals = capability.find('=');
                    if (equals != std::string_view::npos) capability = capability.substr(0, equals);
                    if (!capability.empty()) {
                        if (enabled) wire->capabilities.emplace(capability);
                        else wire->capabilities.erase(std::string(capability));
                    }
                    if (end == std::string_view::npos) break;
                    list.remove_prefix(end + 1);
                }
            };
            if (p[1] == "ACK" || p[1] == "NEW") update(p.back(), true);
            else if (p[1] == "DEL") update(p.back(), false);
        } else if (command == "NICK" && !p.empty() && SameTarget(nick, nickname_)) {
            nickname_ = p.front();
        } else if ((command == "JOIN" || command == "PART") && !p.empty() && SameTarget(nick, nickname_)) {
            Membership(p.front(), command == "JOIN");
        } else if (command == "KICK" && p.size() >= 2 && SameTarget(p[1], nickname_)) {
            Membership(p[0], false);
        } else if (command == "PRIVMSG" && p.size() >= 2) {
            Receive(nick, p[0], p[1]);
        } else if ((command == "403" || command == "405" || command == "437" || command == "470" ||
                    command == "471" || command == "473" || command == "474" || command == "475" ||
                    command == "476" || command == "477" || command == "489") && p.size() >= 2) {
            const auto reason = ReplyReason(message, "IRC server refused to join the channel.");
            if (report_) report_({true, true, reason});
            if (emit_) emit_(IncomingConnectionFailure{kIrcProviderId, TransferFailure::ConnectionFailure,
                profile.name + ": " + reason});
        }
    }

    static bool RequiresSasl(std::string_view reason) {
        const auto folded = Fold(reason, "ascii");
        return folded.find("sasl") != std::string::npos && folded.find("required") != std::string::npos;
    }

    std::string ServerText(std::string_view raw) const {
        std::string text;
        for (const unsigned char byte : raw) {
            if (byte >= 32 && byte != 127) text += static_cast<char>(byte);
        }
        if (!Utf8(text)) return {};
        const auto redact = [&](std::string_view secret) {
            if (secret.empty()) return;
            constexpr std::string_view hidden = "[redacted]";
            for (std::size_t at = 0; (at = text.find(secret, at)) != std::string::npos; at += hidden.size()) {
                text.replace(at, secret.size(), hidden);
            }
        };
        if (!profile.sasl_password.empty()) {
            for (const auto& line : PlainAuthentication(profile.sasl_account, profile.sasl_password)) {
                const auto encoded = std::string_view(line).substr(13, line.size() - 15);
                if (encoded != "+") redact(encoded);
            }
        }
        redact(profile.sasl_password);
        redact(profile.server_password);
        redact(profile.sasl_account);
        return text;
    }

    std::string ReplyReason(const Message& message, std::string fallback) const {
        const auto& params = message.parameters;
        const auto reason = params.size() >= 2 ? ServerText(params.back()) : std::string{};
        return "IRC " + std::string(message.command) + ": " + (reason.empty() ? fallback : reason);
    }

    static std::string WithNotice(const Wire& wire, std::string message) {
        if (!wire.registered && !wire.registration_notice.empty()) message += " " + wire.registration_notice;
        return message;
    }

    void EndCapabilities(const std::shared_ptr<Wire>& wire) {
        wire->registration = Wire::Registration::Ended;
        Queue(wire, "CAP END\r\n");
    }

    void RegisterReply(const std::shared_ptr<Wire>& wire, const Message& message) {
        using Registration = Wire::Registration;
        const auto& p = message.parameters;
        const auto command = message.command;
        if (command == "CAP" && p.size() >= 3) {
            if (p[1] == "LS" && wire->registration == Registration::Listing) {
                wire->offers_sasl_plain |= OffersSaslPlain(p.back());
                if (p.size() >= 4 && p[2] == "*") return;
                if (profile.sasl_account.empty()) return EndCapabilities(wire);
                if (!wire->offers_sasl_plain) return Fail("IRC server does not advertise SASL PLAIN.", true);
                wire->registration = Registration::RequestingSasl;
                Queue(wire, "CAP REQ :sasl\r\n");
            } else if (wire->registration == Registration::RequestingSasl) {
                if (p[1] == "NAK") return Fail("IRC server refused the SASL capability.", true);
                if (p[1] == "ACK" && OffersSaslPlain(p.back())) {
                    wire->registration = Registration::Challenge;
                    Queue(wire, "AUTHENTICATE PLAIN\r\n");
                }
            }
        } else if (command == "AUTHENTICATE" && p.size() == 1 && p[0] == "+" &&
                   wire->registration == Registration::Challenge) {
            wire->registration = Registration::Authenticating;
            for (auto& line : PlainAuthentication(profile.sasl_account, profile.sasl_password)) {
                Queue(wire, std::move(line));
            }
        } else if (command == "903" && wire->registration == Registration::Authenticating) {
            EndCapabilities(wire);
        } else if (command == "421" && p.size() >= 2 && p[1] == "CAP") {
            if (!profile.sasl_account.empty()) Fail(ReplyReason(message, "IRC server does not support CAP."), true);
        } else if (command == "902" || command == "904" || command == "905" || command == "906" ||
                   command == "907" || command == "431" || command == "432" || command == "433" ||
                   command == "436" || command == "437" || command == "451" || command == "461" ||
                   command == "462" || command == "463" || command == "464" || command == "465" ||
                   command == "466" || command == "468" || command == "484") {
            Fail(ReplyReason(message, "IRC registration was rejected."), true);
        } else if (command == "001") {
            if (!profile.sasl_account.empty() && wire->registration != Registration::Ended) {
                return Fail("IRC server registered without the requested SASL authentication.", true);
            }
            if (!p.empty()) nickname_ = p.front();
            wire->registered = true;
            failures_ = 0;
            if (test_) {
                FinishTest(profile.tls ? "TLS verified. IRC registration succeeded."
                                        : "IRC registration succeeded over plain TCP.");
                Stop();
                return;
            }
            Publish({true, true, profile.tls ? "Connected with verified TLS" : "Connected over plain TCP"});
            Arm(wire, 90s, "IRC server stopped responding.");
            for (const auto& channel : wanted_) Queue(wire, "JOIN " + channel + "\r\n");
        }
    }

    void Membership(std::string_view target, bool joined) {
        if (!IsIrcChannel(target) || !Token(target) || !Utf8(target)) return;
        auto& peer = EnsurePeer(std::string(target), true);
        peer.joined = joined;
        PeerEvent(Fold(target, mapping_), peer);
        if (report_ && Online()) {
            report_({true, true, joined ? "Joined " + std::string(target)
                                      : "Left " + std::string(target)});
        }
    }

    void Receive(std::string_view nick, std::string_view target, std::string_view text) {
        const bool channel = IsIrcChannel(target);
        if (!channel && !SameTarget(target, nickname_)) return;
        const auto route = channel ? target : nick;
        if (!Token(route) || !Utf8(route) || !Nickname(nick) || !Utf8(nick) || !Utf8(text)) return;
        EnsurePeer(std::string(route), channel);
        std::string content = channel ? "<" + std::string(nick) + "> " : "";
        for (unsigned char ch : text) {
            if (ch >= 32 && ch != 127) content += static_cast<char>(ch);
        }
        if (emit_) emit_(ReceivedContent{{{kIrcProviderId, profile.id + "/" + Fold(route, mapping_)},
            "irc-message-" + std::to_string(++messages_)}, {kTextTypeId, {}, {}, content.size()},
            TextPayload{std::move(content)}});
    }

    void CloseWire() {
        auto wire = std::move(wire_);
        if (!wire) return;
        wire->timer.cancel();
        wire->resolver.cancel();
        Error ignored;
        wire->stream.lowest_layer().close(ignored);
        // Keep the active write buffer alive until its handler runs.
        for (auto& write : wire->writes) if (write.done) {
            auto done = std::move(write.done);
            done(false);
        }
    }

    void FinishTest(std::string message) {
        auto done = std::move(test_);
        if (done) done(std::move(message));
    }

    void Fail(std::string message, bool permanent = false) {
        permanent = permanent || (wire_ && !wire_->registered && RequiresSasl(message));
        CloseWire();
        if (test_) {
            enabled_ = false;
            FinishTest(std::move(message));
            return;
        }
        if (!enabled_) return;
        const bool retry = profile.auto_reconnect && !permanent;
        enabled_ = retry;
        if (retry) {
            const auto delay = std::chrono::seconds(1 << std::min(failures_++, 5u));
            message += " Reconnecting in " + std::to_string(delay.count()) + "s.";
            retry_.expires_after(delay);
            retry_.async_wait([self = shared_from_this()](const Error& ec) {
                if (!ec && self->enabled_) self->Connect();
            });
        }
        if (emit_) emit_(IncomingConnectionFailure{kIrcProviderId,
            TransferFailure::ConnectionFailure, profile.name + ": " + message});
        Publish({retry, false, std::move(message)});
    }

    asio::io_context& io_;
    asio::steady_timer retry_;
    ProviderEventSink emit_;
    Report report_;
    IrcProvider::TestResult test_;
    std::shared_ptr<Wire> wire_;
    std::string nickname_;
    std::string mapping_ = "rfc1459";
    std::vector<std::string> wanted_;
    std::map<std::string, Target> peers_;
    std::uint64_t messages_ = 0;
    unsigned failures_ = 0;
    bool enabled_ = false;
};

} // namespace

class IrcProvider::Impl {
public:
    explicit Impl(std::function<void()> changed) : changed(std::move(changed)) {}
    using Guard = asio::executor_work_guard<asio::io_context::executor_type>;

    ProviderResult Start(ProviderEventSink sink) {
        std::lock_guard lifecycle_lock(lifecycle);
        std::lock_guard lock(mutex);
        if (worker.joinable() || !sink) return {false, "IRC provider cannot start."};
        emit = std::move(sink);
        io.restart();
        guard.emplace(io.get_executor());
        accepting = true;
        try {
            worker = std::thread([this] { io.run(); });
        } catch (const std::system_error&) {
            accepting = false;
            guard.reset();
            emit = {};
            return {false, "Could not start the IRC worker."};
        }
        for (const auto& profile : profiles) {
            if (profile.start_on_launch) asio::post(io, [this, profile] { StartServer(profile); });
        }
        return {true, {}};
    }

    void Stop() noexcept {
        std::lock_guard lifecycle_lock(lifecycle);
        {
            std::lock_guard lock(mutex);
            if (!accepting) return;
            accepting = false;
            asio::post(io, [this] {
                for (auto& [id, session] : sessions) session->Stop();
                sessions.clear();
                for (auto test : tests) if (test) test->Stop();
                tests.clear();
                guard.reset();
            });
        }
        worker.join();
        emit = {};
    }

    void StartServer(const IrcProfile& profile) {
        if (!ValidateIrcProfile(profile).empty()) return;
        GetSession(profile)->Start();
    }

    std::shared_ptr<Session> GetSession(const IrcProfile& profile) {
        auto& session = sessions[profile.id];
        if (!session) session = std::make_shared<Session>(io, profile, emit,
            [this, id = profile.id](IrcStatus status) {
                {
                    std::lock_guard lock(mutex);
                    statuses[id] = std::move(status);
                }
                if (changed) changed();
            });
        return session;
    }

    void SetProfiles(std::vector<IrcProfile> values) {
        std::lock_guard lock(mutex);
        profiles = std::move(values);
        if (!accepting) return;
        asio::post(io, [this, profiles = profiles] {
            for (auto it = sessions.begin(); it != sessions.end();) {
                const auto found = std::find_if(profiles.begin(), profiles.end(),
                    [&](const IrcProfile& p) { return SameProfile(it->second->profile, p); });
                if (found != profiles.end()) {
                    ++it;
                } else {
                    it->second->Stop("Settings changed. Start IRC to reconnect.");
                    it = sessions.erase(it);
                }
            }
        });
    }

    ProviderResult UseProfile(const std::string& id, std::function<void(const IrcProfile&)> action) {
        std::lock_guard lock(mutex);
        if (!accepting) return {false, "IRC provider is unavailable."};
        const auto found = std::find_if(profiles.begin(), profiles.end(),
            [&](const IrcProfile& profile) { return profile.id == id; });
        if (found == profiles.end()) return {false, "Select a saved IRC server."};
        const auto error = ValidateIrcProfile(*found);
        if (!error.empty()) return {false, error};
        asio::post(io, [profile = *found, action = std::move(action)] { action(profile); });
        return {true, {}};
    }

    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::vector<IrcProfile> profiles;
    std::map<std::string, IrcStatus> statuses;
    asio::io_context io;
    std::optional<Guard> guard;
    std::thread worker;
    bool accepting = false;
    ProviderEventSink emit;
    std::function<void()> changed;
    std::map<std::string, std::shared_ptr<Session>> sessions;
    std::vector<std::shared_ptr<Session>> tests;
};

IrcProvider::IrcProvider(std::function<void()> changed) : impl_(std::make_unique<Impl>(std::move(changed))) {}
IrcProvider::~IrcProvider() { Stop(); }

std::vector<Capability> IrcProvider::Capabilities() const { return {{kTextTypeId, {}}}; }
ProviderResult IrcProvider::Start(ProviderEventSink sink) { return impl_->Start(std::move(sink)); }
void IrcProvider::Stop() noexcept { impl_->Stop(); }

void IrcProvider::SetProfiles(std::vector<IrcProfile> profiles) { impl_->SetProfiles(std::move(profiles)); }
std::vector<IrcProfile> IrcProvider::Profiles() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->profiles;
}

IrcStatus IrcProvider::Status(const std::string& id) const {
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->statuses.find(id);
    return found == impl_->statuses.end() ? IrcStatus{} : found->second;
}

ProviderResult IrcProvider::StartServer(const std::string& id) {
    return impl_->UseProfile(id, [this](const IrcProfile& p) { impl_->StartServer(p); });
}

ProviderResult IrcProvider::StopServer(const std::string& id) {
    return impl_->UseProfile(id, [this](const IrcProfile& p) {
        const auto it = impl_->sessions.find(p.id);
        if (it != impl_->sessions.end()) it->second->Stop();
    });
}

ProviderResult IrcProvider::Join(const std::string& id, std::string channel) {
    if (!IsIrcChannel(channel) || !Token(channel) || !Utf8(channel) || channel.size() < 2) {
        return {false, "Enter a single IRC channel, such as #example."};
    }
    if (!Status(id).online) return {false, "Connect to the IRC server before joining a channel."};
    return impl_->UseProfile(id, [this, channel = std::move(channel)](const IrcProfile& p) {
        impl_->GetSession(p)->Join(channel);
    });
}

ProviderResult IrcProvider::Part(const std::string& id, std::string channel) {
    if (!IsIrcChannel(channel) || !Token(channel) || !Utf8(channel)) return {false, "Select an IRC channel."};
    return impl_->UseProfile(id, [this, channel = std::move(channel)](const IrcProfile& p) {
        impl_->GetSession(p)->Part(channel);
    });
}

ProviderResult IrcProvider::OpenPrivate(const std::string& id, std::string nickname) {
    if (!Nickname(nickname) || !Utf8(nickname)) return {false, "Enter an IRC nickname."};
    return impl_->UseProfile(id, [this, nickname = std::move(nickname)](const IrcProfile& p) {
        impl_->GetSession(p)->Open(nickname);
    });
}

ProviderResult IrcProvider::Send(OutgoingTransfer transfer) {
    const auto& peer = transfer.identity.peer;
    auto* text = std::get_if<TextPayload>(&transfer.payload);
    const auto slash = peer.peer_id.find('/');
    if (peer.provider_id != kIrcProviderId || slash == std::string::npos || slash + 1 == peer.peer_id.size()) {
        return {false, "Select an IRC channel or private conversation."};
    }
    if (transfer.metadata.type_id != kTextTypeId || transfer.metadata.schema_version || !text ||
        text->utf8.empty() || text->utf8.size() > 65536 || !Utf8(text->utf8) ||
        text->utf8.find('\0') != std::string::npos) return {false, "IRC supports UTF-8 text up to 64 KiB."};
    const auto id = peer.peer_id.substr(0, slash);
    const auto target = peer.peer_id.substr(slash + 1);
    if (!Status(id).online) return {false, "Connect to the IRC server before sending."};
    return impl_->UseProfile(id,
        [this, identity = std::move(transfer.identity), target, body = std::move(text->utf8)](const IrcProfile& p) {
            impl_->GetSession(p)->Send(identity, target, body);
        });
}

ProviderResult IrcProvider::Test(IrcProfile profile, TestResult done) {
    const auto error = ValidateIrcProfile(profile);
    if (!error.empty()) return {false, error};
    std::lock_guard lock(impl_->mutex);
    if (!impl_->accepting || !done) return {false, "IRC connection test is unavailable."};
    asio::post(impl_->io, [this, profile = std::move(profile), done = std::move(done)]() mutable {
        auto& tests = impl_->tests;
        const auto finished = std::find(tests.begin(), tests.end(), nullptr);
        auto slot = finished == tests.end() ? tests.size() : static_cast<std::size_t>(finished - tests.begin());
        if (slot == tests.size()) tests.push_back(nullptr);
        auto session = std::make_shared<Session>(impl_->io, std::move(profile), ProviderEventSink{}, Session::Report{},
            [this, slot, done = std::move(done)](std::string result) {
                impl_->tests[slot].reset();
                done(std::move(result));
            });
        tests[slot] = session;
        session->Start();
    });
    return {true, {}};
}

ProviderResult IrcProvider::Accept(const TransferIdentity&, std::unique_ptr<std::ostream>) {
    return {false, "IRC does not offer incoming transfers."};
}

ProviderResult IrcProvider::Decline(const TransferIdentity&, std::string) {
    return {false, "IRC does not offer incoming transfers."};
}

ProviderResult IrcProvider::Cancel(const TransferIdentity&, TransferDirection) {
    return {false, "IRC messages cannot be cancelled after queuing."};
}

} // namespace textlt::connect
