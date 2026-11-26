#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "identity.hpp"
#include "providers/irc/protocol.hpp"
#include "textlt/connect/irc_provider.hpp"

namespace {
using namespace std::chrono_literals;
using namespace textlt::connect;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using asio::ip::tcp;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Fixture {
public:
    using Script = std::function<void(Fixture&)>;

    explicit Fixture(Script script)
        : acceptor_(io_, {asio::ip::make_address("127.0.0.1"), 0}),
          port_(acceptor_.local_endpoint().port()) {
        acceptor_.non_blocking(true);
        worker_ = std::thread([this, script = std::move(script)] {
            try {
                script(*this);
            } catch (...) {
                error_ = std::current_exception();
            }
        });
    }

    ~Fixture() {
        if (worker_.joinable()) worker_.join();
    }

    std::uint16_t Port() const { return port_; }

    tcp::socket Accept() {
        tcp::socket socket(io_);
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        do {
            boost::system::error_code error;
            acceptor_.accept(socket, error);
            if (!error) return socket;
            if (error != asio::error::would_block && error != asio::error::try_again) {
                throw boost::system::system_error(error);
            }
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("The IRC client did not connect to the fixture.");
    }

    void Finish() {
        worker_.join();
        if (error_) std::rethrow_exception(error_);
    }

private:
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::uint16_t port_;
    std::thread worker_;
    std::exception_ptr error_;
};

template<class Stream>
std::string Line(Stream& stream, asio::streambuf& buffer) {
    asio::read_until(stream, buffer, '\n');
    std::istream input(&buffer);
    std::string line;
    std::getline(input, line);
    return line + "\n";
}

template<class Stream>
void Write(Stream& stream, std::string_view data) {
    asio::write(stream, asio::buffer(data));
}

class Events {
public:
    void Add(ProviderEvent event) {
        std::lock_guard lock(mutex_);
        events_.push_back(std::move(event));
        cv_.notify_all();
    }

    template<class Event, class Predicate>
    bool Wait(Predicate match) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, 5s, [&] {
            for (const auto& event : events_) {
                const auto* typed = std::get_if<Event>(&event);
                if (typed && match(*typed)) return true;
            }
            return false;
        });
    }

    bool Text(const std::string& peer, const std::string& expected) {
        return Wait<ReceivedContent>([&](const ReceivedContent& event) {
            const auto* text = std::get_if<TextPayload>(&event.payload);
            return event.identity.peer.peer_id == peer && text && text->utf8 == expected;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<ProviderEvent> events_;
};

IrcProfile Profile(std::string id, std::uint16_t port) {
    IrcProfile profile;
    profile.id = std::move(id);
    profile.name = profile.id;
    profile.server = "127.0.0.1";
    profile.port = port;
    profile.nickname = "tester";
    profile.username = "textlt";
    profile.tls = false;
    profile.channels = {"#room"};
    return profile;
}

void Register(tcp::socket& socket, asio::streambuf& buffer, bool confirm_join = true) {
    Require(Line(socket, buffer) == "CAP LS 302\r\n", "Expected IRC capability discovery.");
    Require(Line(socket, buffer) == "NICK tester\r\n", "Expected NICK.");
    Require(Line(socket, buffer) == "USER textlt 0 * :textlt\r\n", "Expected USER.");
    Write(socket, "PING :fixture\r\n");
    Require(Line(socket, buffer) == "PONG :fixture\r\n", "Expected response to registration PING.");
    Write(socket, ":fixture CAP tester LS * :multi-prefix\r\n"
                  ":fixture CAP tester LS :sasl=EXTERNAL,PLAIN\r\n");
    Require(Line(socket, buffer) == "CAP END\r\n", "End capability discovery without SASL.");
    Write(socket, ":fixture 001 tester :Welcome\r\n:fixture 005 tester CASEMAPPING=ascii :supported\r\n");
    Require(Line(socket, buffer) == "JOIN #room\r\n", "Expected auto-join.");
    if (confirm_join) Write(socket, ":tester!test@fixture JOIN :#room\r\n");
}

void TestPlain() {
    std::string long_text;
    for (int i = 0; i < 300; ++i) long_text += "хі";
    const auto expected = irc_detail::Privmsg("#room", long_text);
    Require(expected.size() > 1, "Split UTF-8 by IRC line size.");
    std::string reconstructed;
    for (const auto& line : expected) {
        Require(line.size() <= 512 && irc_detail::Utf8(line), "Keep every IRC line valid and within 512 bytes.");
        reconstructed += line.substr(15, line.size() - 17);
    }
    Require(reconstructed == long_text, "Preserve every UTF-8 byte through splitting.");
    Require(irc_detail::Privmsg("#room", "one\r\nQUIT\n")[1] == "PRIVMSG #room :QUIT\r\n",
        "Newlines must not inject IRC commands.");
    const auto auth = irc_detail::PlainAuthentication("a", std::string(297, 'p'));
    Require(auth.size() == 2 && auth[0].size() == 415 && auth[1] == "AUTHENTICATE +\r\n",
        "Terminate a full-size SASL chunk.");

    std::promise<void> allow_join;
    auto join_ready = allow_join.get_future();
    Fixture first([&](Fixture& server) {
        auto socket = server.Accept();
        asio::streambuf buffer;
        Register(socket, buffer, false);
        Write(socket, ":other!u@h JOIN #room\r\n"
                      ":Ada!u@h PRIVMSG #room :before JOIN confirmation\r\n");
        Require(join_ready.wait_for(5s) == std::future_status::ready, "Wait for the unconfirmed channel check.");
        Write(socket, ":tester!test@fixture JOIN :#room\r\n");
        Write(socket, "@time=fixture :Ada!u@h PRIVMSG #ROOM :привіт\r\n"
                      ":Ada!u@h PRIVMSG tester :hello\r\n");
        for (const auto& line : expected) Require(Line(socket, buffer) == line, "Send all PRIVMSG chunks.");
        Require(Line(socket, buffer) == "JOIN #extra\r\n", "Expected manual JOIN.");
        Write(socket, ":tester!u@h JOIN #extra\r\n");
        Require(Line(socket, buffer) == "PART #extra\r\n", "Expected manual PART.");
        Write(socket, ":tester!u@h PART #extra\r\n");
        Require(Line(socket, buffer) == "PRIVMSG Ada :private reply\r\n", "Expected private PRIVMSG.");
        Write(socket, ":op!u@h KICK #room tester :removed by operator\r\n");
        std::this_thread::sleep_for(25ms);
        socket.close();
        buffer.consume(buffer.size());
        socket = server.Accept();
        Register(socket, buffer);
        Write(socket, ":Ada!u@h PRIVMSG #room :back\r\n");
        Require(Line(socket, buffer) == "QUIT :TextLT closing\r\n", "Expected graceful QUIT.");
    });
    Fixture second([](Fixture& server) {
        auto socket = server.Accept();
        asio::streambuf buffer;
        Register(socket, buffer);
        Write(socket, ":Ada!u@h PRIVMSG #room :another server\r\n");
        Require(Line(socket, buffer) == "QUIT :TextLT closing\r\n", "Expected QUIT on second server.");
    });
    Events events;
    IrcProvider provider;
    auto other = Profile("second", second.Port());
    other.start_on_launch = true;
    provider.SetProfiles({Profile("first", first.Port()), other});
    Require(provider.Start([&](ProviderEvent event) { events.Add(std::move(event)); }).accepted, "Start IRC.");
    Require(!provider.Join("first", "#offline").accepted, "Reject JOIN while disconnected.");
    Require(provider.StartServer("first").accepted, "Start saved server.");
    Require(events.Text("first/#room", "<Ada> before JOIN confirmation"), "Receive the unconfirmed channel fixture.");
    Require(provider.Send({{{kIrcProviderId, "first/#room"}, "unconfirmed"}, {kTextTypeId},
        TextPayload{"must wait"}}).accepted, "Queue a check before JOIN confirmation.");
    Require(events.Wait<TransferEvent>([](const TransferEvent& event) {
        return event.identity.transfer_id == "unconfirmed" && event.state == TransferState::Failed;
    }), "Refuse channel messages until the server confirms our own JOIN.");
    allow_join.set_value();
    Require(events.Text("first/#room", "<Ada> привіт"), "Receive UTF-8 channel text.");
    Require(provider.Status("first").online, "Report IRC online only after numeric 001.");
    Require(events.Wait<Peer>([](const Peer& peer) {
        return peer.identity.peer_id == "first/#room" && peer.online;
    }), "Confirm channel membership from our own JOIN response.");
    Require(events.Text("second/#room", "<Ada> another server"), "Separate identical channels on different servers.");
    Require(events.Text("first/ada", "hello"), "Route private message to its sender.");
    Require(provider.OpenPrivate("first", "Bea").accepted &&
        events.Wait<Peer>([](const Peer& peer) { return peer.identity.peer_id == "first/bea"; }),
        "Open a private conversation without waiting for an incoming message.");
    const auto send = [&](std::string peer, std::string text, std::string id) {
        return provider.Send({{{kIrcProviderId, std::move(peer)}, std::move(id)}, {kTextTypeId},
            TextPayload{std::move(text)}}).accepted;
    };
    Require(send("first/#room", long_text, "long"), "Queue channel text.");
    Require(events.Wait<TransferEvent>([](const TransferEvent& event) {
        return event.identity.transfer_id == "long" && event.state == TransferState::Completed;
    }), "Report sent IRC text.");
    Require(provider.Join("first", "#extra").accepted, "Join channel.");
    Require(events.Wait<Peer>([](const Peer& peer) { return peer.identity.peer_id == "first/#extra" && peer.online; }),
        "Show joined channel.");
    Require(provider.Part("first", "#extra").accepted, "Part channel.");
    Require(events.Wait<Peer>([](const Peer& peer) {
        return peer.identity.peer_id == "first/#extra" && !peer.online;
    }), "Mark channel offline after the server confirms departure.");
    Require(send("first/ada", "private reply", "private"), "Queue private text.");
    Require(events.Wait<Peer>([](const Peer& peer) {
        return peer.identity.peer_id == "first/#room" && !peer.online;
    }), "Mark channel offline after an unexpected KICK.");
    Require(events.Text("first/#room", "<Ada> back"), "Reconnect and rejoin the saved channel.");
    Require(provider.StopServer("first").accepted, "Stop one IRC server.");
    first.Finish();
    Require(provider.Status("second").online, "Keep the other server connected.");
    provider.Stop();
    second.Finish();
}

class TestResult {
public:
    IrcProvider::TestResult Callback() {
        return [this](std::string value) {
            std::lock_guard lock(mutex_);
            value_ = std::move(value);
            cv_.notify_all();
        };
    }

    std::string Wait() {
        std::unique_lock lock(mutex_);
        Require(cv_.wait_for(lock, 5s, [&] { return !value_.empty(); }), "Wait for IRC diagnostics.");
        return value_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string value_;
};

void TestTls(const std::filesystem::path& path) {
    Require(detail::EnsureIdentityFile(path, "localhost"), "Generate a loopback certificate.");
#ifdef _WIN32
    Require(_putenv_s("SSL_CERT_FILE", path.string().c_str()) == 0, "Trust the loopback certificate.");
#else
    Require(setenv("SSL_CERT_FILE", path.string().c_str(), 1) == 0, "Trust the loopback certificate.");
#endif
    ssl::context context(ssl::context::tls_server);
    context.use_certificate_chain_file(path.string());
    context.use_private_key_file(path.string(), ssl::context::pem);
    Fixture valid([&](Fixture& server) {
        ssl::stream<tcp::socket> stream(server.Accept(), context);
        stream.handshake(ssl::stream_base::server);
        asio::streambuf buffer;
        Require(Line(stream, buffer) == "PASS :server-secret\r\n", "Send PASS over TLS.");
        Require(Line(stream, buffer) == "CAP LS 302\r\n", "Discover capabilities over TLS.");
        Require(Line(stream, buffer) == "NICK tester\r\n", "Register TLS nickname.");
        Require(Line(stream, buffer) == "USER textlt 0 * :textlt\r\n", "Register TLS username.");
        Write(stream, ":fixture CAP tester LS * :sasl=EXTERNAL,PLAIN\r\n"
                      ":fixture CAP tester LS :multi-prefix\r\n");
        Require(Line(stream, buffer) == "CAP REQ :sasl\r\n", "Request advertised SASL capability.");
        Write(stream, ":fixture CAP tester ACK :sasl\r\n");
        Require(Line(stream, buffer) == "AUTHENTICATE PLAIN\r\n", "Start SASL PLAIN.");
        Write(stream, "AUTHENTICATE +\r\n");
        const auto auth = irc_detail::PlainAuthentication("account", "password");
        Require(Line(stream, buffer) == auth.front(), "Send SASL credentials.");
        Write(stream, ":fixture 903 tester :SASL successful\r\n");
        Require(Line(stream, buffer) == "CAP END\r\n", "Complete capability negotiation.");
        Write(stream, ":fixture 001 tester :welcome\r\n");
        Require(Line(stream, buffer) == "QUIT :TextLT closing\r\n", "Connection test quits without auto-joining.");
    });
    Fixture wrong_host([&](Fixture& server) {
        ssl::stream<tcp::socket> stream(server.Accept(), context);
        boost::system::error_code error;
        stream.handshake(ssl::stream_base::server, error);
        Require(static_cast<bool>(error), "Reject a certificate with a different hostname.");
    });
    TestResult good, bad;
    IrcProvider provider;
    Require(provider.Start([](ProviderEvent) {}).accepted, "Start IRC diagnostics.");
    auto profile = Profile("tls", valid.Port());
    profile.sasl_account = "account";
    profile.sasl_password = "password";
    profile.server_password = "server-secret";
    Require(!ValidateIrcProfile(profile).empty(), "Reject SASL over plain TCP.");
    profile.tls = true;
    profile.server = "localhost";
    Require(provider.Test(profile, good.Callback()).accepted, "Run authenticated TLS connection test.");
    Require(good.Wait() == "TLS verified. IRC registration succeeded.", "Verify IRC registration with SASL.");
    valid.Finish();

    profile.server = "127.0.0.1";
    profile.port = wrong_host.Port();
    Require(provider.Test(profile, bad.Callback()).accepted, "Run mismatched certificate test.");
    const auto diagnostic = bad.Wait();
    Require(diagnostic == "IRC TLS certificate validation failed.", "Report a redacted TLS validation failure.");
    wrong_host.Finish();
}

void TestRegistrationFailures() {
    Fixture policy([](Fixture& server) {
        auto socket = server.Accept();
        asio::streambuf buffer;
        Require(Line(socket, buffer) == "CAP LS 302\r\n", "Discover capabilities before rejection.");
        Require(Line(socket, buffer) == "NICK tester\r\n", "Send NICK before capability response.");
        Require(Line(socket, buffer) == "USER textlt 0 * :textlt\r\n", "Send USER before capability response.");
        Write(socket, ":fixture CAP tester LS :sasl=PLAIN\r\n");
        Require(Line(socket, buffer) == "CAP END\r\n", "Finish capability negotiation without credentials.");
        Write(socket, ":fixture NOTICE tester :SASL authentication is required on this network.\r\n"
                      "ERROR :SASL authentication to a verified NickServ account is required.\r\n");
    });
    Fixture numeric([](Fixture& server) {
        auto socket = server.Accept();
        asio::streambuf buffer;
        Require(Line(socket, buffer) == "PASS :fixture-secret\r\n", "Send the fixture password.");
        for (int i = 0; i < 3; ++i) Line(socket, buffer);
        Write(socket, ":fixture 464 tester :Password fixture-secret was rejected by this server.\r\n");
    });
    TestResult error, rejected;
    IrcProvider provider;
    Require(provider.Start([](ProviderEvent) {}).accepted, "Start IRC diagnostics.");
    Require(provider.Test(Profile("policy", policy.Port()), error.Callback()).accepted,
        "Test a server that requires SASL.");
    Require(error.Wait() == "IRC server error: SASL authentication to a verified NickServ account is required.",
        "Show the actual server ERROR instead of announcing registration.");
    policy.Finish();

    auto profile = Profile("numeric", numeric.Port());
    profile.server_password = "fixture-secret";
    Require(provider.Test(profile, rejected.Callback()).accepted, "Test a server that rejects the password.");
    Require(rejected.Wait() == "IRC 464: Password [redacted] was rejected by this server.",
        "Keep numeric diagnostics and redact credentials.");
    numeric.Finish();
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("textlt-irc-loopback-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pem");
    int result = 0;
    try {
        TestPlain();
        TestTls(path);
        TestRegistrationFailures();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return result;
}
