#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "textlt/connect/provider.hpp"

namespace textlt::connect {

inline constexpr char kIrcProviderId[] = "irc";

struct IrcProfile {
    std::string id;
    std::string name = "Libera Chat";
    std::string server = "irc.libera.chat";
    std::uint16_t port = 6697;
    bool tls = true;
    std::string nickname;
    std::string username;
    std::string sasl_account;
    std::string sasl_password;
    std::string server_password;
    std::vector<std::string> channels;
    bool auto_reconnect = true;
    bool start_on_launch = false;
};

struct IrcStatus {
    bool running = false;
    bool online = false;
    std::string diagnostic = "Stopped";
};

std::string ValidateIrcProfile(const IrcProfile& profile);
std::string IrcServerFromPeer(std::string_view peer_id);
bool IsIrcChannel(std::string_view target);

class IrcProvider final : public IConnectProvider {
public:
    using TestResult = std::function<void(std::string)>;

    explicit IrcProvider(std::function<void()> state_changed = {});
    ~IrcProvider() override;

    std::string Id() const override { return kIrcProviderId; }
    std::vector<Capability> Capabilities() const override;
    ProviderResult Start(ProviderEventSink sink) override;
    void Stop() noexcept override;
    ProviderResult Send(OutgoingTransfer transfer) override;
    ProviderResult Accept(const TransferIdentity&, std::unique_ptr<std::ostream>) override;
    ProviderResult Decline(const TransferIdentity&, std::string) override;
    ProviderResult Cancel(const TransferIdentity&, TransferDirection) override;

    void SetProfiles(std::vector<IrcProfile> profiles);
    std::vector<IrcProfile> Profiles() const;
    IrcStatus Status(const std::string& id) const;
    ProviderResult StartServer(const std::string& id);
    ProviderResult StopServer(const std::string& id);
    ProviderResult Join(const std::string& id, std::string channel);
    ProviderResult Part(const std::string& id, std::string channel);
    bool Joined(const std::string& id, std::string channel) const;
    ProviderResult OpenPrivate(const std::string& id, std::string nickname);
    ProviderResult Test(IrcProfile profile, TestResult done);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace textlt::connect
