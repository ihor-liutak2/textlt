# TextLT Connect

This directory is the home of Connect, the component for exchanging text, files,
and structured items on a local network. See the
[Connect architecture](../docs/connect-architecture.md) for the public contracts,
provider capabilities, and implementation sequence.

## Layout

| Path within `connect/` | Responsibility |
| --- | --- |
| `CMakeLists.txt` | Build the standalone C++17 static library. |
| `include/textlt/connect/types.hpp` | Peer identity, capabilities, transfer metadata, payloads, offers, and events. |
| `include/textlt/connect/service.hpp` | `ConnectService` registration and lookup for providers and structured handlers. |
| `include/textlt/connect/provider.hpp` | `IConnectProvider` operations and the event callback contract. |
| `include/textlt/connect/content_handler.hpp` | `ContentHandler` validation, preview, and import contract. |
| `include/textlt/connect/textlt_provider.hpp` | TextLT provider factory, IPv4 endpoints, pinned TLS, and discovery options. |
| `src/service.cpp` | Provider and structured handler registry implementation. |
| `src/providers/localsend/` | LocalSend v2.2 multicast discovery and HTTPS transfers through Boost.Beast. |
| `src/providers/textlt/` | TextLT provider, TCP sockets, mutual TLS, IPv4 multicast discovery, and the versioned text protocol. |
| `ui/` | Editor-only FTXUI workspace, conversations, draft state, and provider event delivery. |
| `examples/textlt_loopback.cpp` | Standalone discovery, sender, and receiver demonstration. |

## Build

Install the OpenSSL 3 development libraries and Boost 1.74 or newer headers
with Beast and Asio. CMake locates them through `find_package(OpenSSL)` and
`find_package(Boost)`. Boost is used header-only. For a separate OpenSSL installation, pass
`-DOPENSSL_ROOT_DIR=/path/to/openssl` during configuration. The two-device
setup also uses the `openssl` command-line tool.

From the repository root, run:

```sh
cmake -S connect -B build/connect
cmake --build build/connect
```

Consumers can add this directory with `add_subdirectory(connect)` and link
`textlt::connect` (the alias for `textlt_connect`). The target supplies the
public include path and C++17 requirement. Connect builds independently of
TextLT's sources and FTXUI.

## Connect in the editor

Choose **Options → Connect** next to Docs, Notes, and Actions. The left
column lists connections by provider and their status. It uses 32 terminal
cells before the border on wide terminals and narrows with the window. The
right column shows the selected conversation, incoming offers, and the
message composer. Use **Send** or Enter in the composer. **Accept** and
**Decline** apply to the selected conversation's offer. An outgoing message
shows **Delivered** after the provider receives the acknowledgment.
Use **Start** or **Stop** under **TextLT** or **LocalSend** in the left
column to control each provider independently. **Refresh peers** sends a
LocalSend discovery announcement while transfers continue.

The **Connect** menu is displayed while this workspace is selected. It opens
TextLT configuration, LocalSend settings, or diagnostics.

| Menu path | Settings |
| --- | --- |
| **Connect → TextLT Config…** | Copy this device's identity JSON, save JSON from other TextLT devices, and switch between saved connections. |
| **Connect → LocalSend…** | Configure LocalSend discovery, peer address, alias, and interface. |
| **TextLT Config → Network** | Configure the TextLT listener address and port and optionally enable LAN discovery. Set the listener port to `0` for sender-only or discovery-only use. Restart TextLT after changing these settings. |
| **Connect → Diagnostics…** | Inspect assigned IPv4 and IPv6 addresses, default gateways, and LocalSend listener state. Select an address to copy it or configure LocalSend. |

TextLT apps found on the LAN appear under **Online apps** in the left column.
Selecting one opens its conversation.

The native provider uses one configured peer fingerprint at a time. TextLT
creates a persistent local identity on first use with a combined certificate
and private key PEM. Existing manually configured identities are used as saved.

To use the saved list, open **TextLT Config → Identity** on both devices and
choose **Copy identity**. Each object includes version `1`, `name`, `peer_id`,
`address`, `port`, `fingerprint`, and a matching `pairing_code`. The address and port fields can share
a forwarded endpoint when the other device needs to reach a different address.
Exchange the JSON directly and paste the other device's object into
**TextLT Config → Connections**. Choose **Save connection**, select it in the
left column and choose **Connect**. Switching restarts only the native TextLT
provider. Saved connections are stored in `textlt-connections.json`.
The **Identity** tab lets you save a new device name. TextLT keeps its certificate
and peer ID across restarts. **Regenerate identity** creates a fresh certificate,
fingerprint, and peer ID after confirmation. Copy the new JSON for your peers
before reconnecting.
The **Files** tab lets you edit the certificate and private-key PEM paths.
**Save paths** checks that the files match. A running listener uses the saved
paths the next time it starts.

Choose **Start** under **TextLT** to look for nearby apps. To connect, first
save the other app's identity JSON under **TextLT Config → Connections**, then
choose **Connect** for that saved app.

TextLT network settings save in `connect-settings.json` beside the editor
configuration. The Network tab shows the detected non-loopback IPv4 address
and uses it as the initial listen address. Native Connect uses TCP port
`53327` by default. Set the listen port to `0` to disable the native listener.
LocalSend settings are under **Connect → LocalSend…**. Conversations and drafts
stay in the current editor session.

Accepting an incoming request remembers that app in `connect-settings.json`.
Future requests from the same app are accepted automatically.
Networking stays active when switching workspaces and stops on application
exit. The workspace uses its own Connections column. Its FTXUI UI lives in
`connect/ui/`, and the settings modal is in `src/modals/`. The editor build
links the standalone Connect library and needs the OpenSSL development
dependency listed above.

## Send a file from the editor

1. Set up a trusted TextLT connection on both instances using the certificate
   steps below. Choose **Start** under **TextLT** on both ends.
2. Select the peer in **Options → Connect**. When the peer advertises file
   support or responds to the manual endpoint's TLS capability check,
   **Send File…** appears in the conversation. Choose a regular file in the
   editor's picker.
3. The receiving conversation shows the filename and size. Choose **Accept**
   to save it in Downloads or **Decline** to skip the transfer. Accepting once
   remembers that app and accepts its future requests automatically. Either
   side can use **Cancel transfer** while a file transfer is pending or active.
4. Both conversations show byte progress and the final outcome. The sender
   shows **Delivered** when the receiver acknowledges the saved file.

On Windows the receiver uses the Downloads known folder. On Linux it uses
the XDG Downloads location when configured. The default is `Downloads`
under the home directory. Connect uses the basename, stages bytes within the
destination, and publishes only a complete file. If the name already exists,
the new file gets a suffix such as `report (1).pdf`. A declined or interrupted
transfer leaves no partial file. The Downloads filesystem must support hard
links for atomic publication without overwriting an existing name.

For a one-host check, run two editor instances with separate profiles and
certificates. Set a different listen port in each instance on the host's detected IPv4 address.
Enter that address and the other instance's port and verified fingerprint in Connections. A
two-device check uses the same workflow with the two LAN addresses. The
plain loopback demo below continues to support text. File transfers require
mutual TLS.

## Share and import a Note or Action

1. Start the trusted TextLT connection on both instances. Open a note or
   select a note card in Notes, or select an action in an Actions page.
   Choose **Notes → Send via Connect…** or **Actions → Send via Connect…**.
2. Connect opens a snapshot picker. Select a peer and choose **Send snapshot**.
   The picker lists trusted peers that reported support for the item's schema
   version over TLS.
3. On the receiving instance, accept the offer. After delivery, click
   **Preview / Import…** in the conversation to inspect the item. Acceptance
   places the snapshot in the conversation.
4. Select a Notes group (or General without a group) or an existing Actions
   page, then choose **Import copy**. Connect shows the import result separately
   from delivery. Each received item can be imported once in the current
   Connect session.

The snapshot preserves the note title and structured blocks, or the action
title, tab captions, tab contents, and hidden-text flags. Import creates a
local copy with fresh identifiers. Action content stays data during preview
and import. See the [wire format and schemas](../docs/connect-architecture.md#textlt-structured-content).
The TLS setup below works on one host with separate profiles, credentials,
and ports, or on two devices with their respective LAN addresses.

## Inspect the network in Connect Settings

Open **Options → Connect**, then **Connect → Diagnostics…**, or select
**Diagnostics** in Connect Settings. The list puts active LAN addresses and
TextLT's selected IPv4 address first. Select a row to see where it can be used.
**Refresh** rereads the interfaces. **Copy IP** copies just the address.

LocalSend normally chooses its interface automatically at Start. To override
the choice, select an address TextLT can bind to and click **Use for LocalSend**.
Apply the change with **Stop** and **Start** under **LocalSend** in the left
column. Clear the interface field in the LocalSend settings tab to restore Auto.

In WSL, the list labels the Windows and Linux addresses. A Windows address
available only to the host can be used by TextLT running in Windows. When WSL
uses NAT, LocalSend also checks the Windows LAN over HTTPS at startup and
when you choose **Refresh peers**. It checks the configured discovery port
on the active Windows LAN subnet, within the host address's /24 block.
A phone needs a Windows TCP port forward to reach the WSL receive listener.
Diagnostics confirms phone access after an authenticated mobile LocalSend
client reaches that listener during the current session.

Open **Info** for adapter details, gateways, the WSL networking mode, and
LocalSend port and bind diagnostics. Setting the HTTPS port to `0` lets the
system assign a free port, which appears in Info after Start.

The **Diagnostics** tab automatically checks TCP and UDP ports 53317 and
53327 without starting LocalSend. **Refresh** reruns those checks.
The tab shows whether each TCP and UDP port is free or in use. On WSL,
it shows separate results for WSL and the Windows host.
**Test discovery** starts LocalSend if stopped and sends a discovery announcement.
In WSL the report checks the Linux guest and Windows host. It includes firewall rules
and Windows forwarding rules that use either port. In WSL NAT mode it
also shows how many devices answered the HTTPS discovery check.
**Copy diagnostics** copies the full report as plain text once the checks finish.

**Troubleshoot** asks which TCP port to check. Select the LocalSend port for
phone transfers or the TextLT port for direct connections with a paired
TextLT device. On WSL NAT, the guide copies one Windows PowerShell command at
a time and asks for the result before continuing. The forwarding command
checks the WSL listener first and keeps rules that point to a different service.

## Exchange text and files with LocalSend

1. Open **Options → Connect → LocalSend…** and set an alias. The default multicast
   group is `224.0.0.167`, the default discovery UDP port is `53317`, and the default HTTPS receive port is
   `53317`. Leave the interface blank for Auto, or select an eligible IPv4
   address in Diagnostics. Close the settings and choose **Start** under
   **LocalSend** in the left column. If another app on the same host already
   owns TCP port `53317`, choose a different HTTPS port for TextLT and keep
   the same discovery group and UDP port in both apps.
2. In the official LocalSend app, enable HTTPS and start its receive service.
   The two applications should appear in each other's device list. In
   TextLT, select the peer under **LocalSend** in Connections.
   If automatic discovery is unavailable, enter the peer's IPv4 address or
   `IPv4:port` in the **LocalSend** tab, then choose **Connect by IP** in the left column. The
   default peer port is `53317`. TextLT selects the peer for this session.
3. Send a message from the composer or pick a regular file with **Send File…**.
   An ordinary `.txt` attachment arrives as a file. Messages over 16,384
   UTF-8 bytes can be sent with **Send as .txt**. If the receiver asks for
   a PIN, enter it in the conversation and choose **Retry**.
4. On the receiving side, accept or decline the offer. Accepted inline
   text appears in the conversation. Files use the Downloads staging and
   collision rules above. The sender shows **Delivered** after the receiver
   confirms receipt. Use **Cancel transfer** to withdraw a pending or
   active transfer.

LocalSend keeps a separate persistent identity in `localsend.pem` beside the
editor configuration. Before sending, TextLT verifies the receiver's TLS
certificate against the fingerprint discovered for that endpoint. Incoming senders without a client
certificate are labelled unverified and always require approval. TextLT's
own certificate pins and the Notes and Actions handlers apply to its native
provider.

Local verification used TextLT 1.0.0 on one Ubuntu 26.04 x86-64 host with
Boost 1.90 and OpenSSL 3.5.5. Two TextLT editor processes used separate
profiles and HTTPS ports on the same IPv4 interface. Adapter-to-adapter and
independent HTTPS checks exercised the v2.2 wire flow. The official LocalSend
app was unavailable on that host, so no official app version or two-device
exchange was verified.

## Loopback demonstration

The standalone build also produces `textlt_connect_loopback`. In one terminal,
start the receiver and wait for it to print its listening address.

```sh
./build/connect/textlt_connect_loopback receive 47651 accept
```

In another terminal, send one text message.

```sh
./build/connect/textlt_connect_loopback send 47651 'Hello from Connect'
```

The receiver prints the offer and the received text, then reports
`Acknowledged message-1`. The sender prints `Delivered message-1` after
receiving the acknowledgment and exits with status 0.

Repeat with the receiver set to decline.

```sh
./build/connect/textlt_connect_loopback receive 47652 decline
```

```sh
./build/connect/textlt_connect_loopback send 47652 'Do not deliver this'
```

Both processes report `Declined message-1`. The sender exits with status 2
and the receiver exits without displaying any message body. The receiver
handles one offer per invocation. Choose a free port if either example port
is in use. With multi-configuration generators, use the executable in the
build configuration directory.

## Trusted text between two devices

Build Connect on both devices. In the commands below, Device B listens on
`192.168.1.42:47651`. Replace that IP with Device B's actual LAN IPv4 address
and allow inbound TCP on that port from Device A if the local firewall
requires it. The listener binds only the selected address.

On each device, generate its own certificate and key. Replace
`textlt-device-a` with `textlt-device-b` on Device B. Keep the private key on
the device that generated it and restrict access using local file permissions.

```sh
umask 077
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -noenc \
  -days 365 -subj '/CN=textlt-device-a' -keyout device.key -out device.crt
openssl x509 -in device.crt -noout -fingerprint -sha256
```

Compare the **full SHA-256 fingerprint** from each device with its owner in
person or over an independently authenticated channel. On each device, set
`PEER_PIN` to the verified fingerprint of the **other** device. Copy the hex
after `=` from the OpenSSL output. Colons are optional. Each command below
uses the local device's certificate and key.

On Device B, start the receiver and wait for its listening line.

```sh
PEER_PIN='paste-device-a-verified-fingerprint-here'
./build/connect/textlt_connect_loopback receive-tls 192.168.1.42 47651 \
  device.crt device.key "$PEER_PIN" accept
```

On Device A, send the message.

```sh
PEER_PIN='paste-device-b-verified-fingerprint-here'
./build/connect/textlt_connect_loopback send-tls 192.168.1.42 47651 \
  device.crt device.key "$PEER_PIN" 'Hello from Device A'
```

Device B prints the offer and received text, then reports
`Acknowledged message-1`. Device A prints `Delivered message-1`. To exercise
the decline flow, restart the receiver with `decline` in place of `accept`.
To exercise certificate rejection, restart the receiver with an incorrect
64-digit hex fingerprint for Device A. It reports a fingerprint mismatch
before publishing an offer. Restore the verified fingerprint afterward.

A replacement certificate has a new fingerprint. Verify it through the same
out-of-band process before updating the configured pin. The `send-tls` and
`receive-tls` commands also work on `127.0.0.1` with two separately generated
certificates.

## Discover and message a trusted LAN peer

Use the certificate setup and out-of-band fingerprint verification above.
Choose unique, persistent peer IDs for the devices. The commands below use
`device-a` and `device-b`, with example LAN addresses `192.168.1.41` and
`192.168.1.42`. Substitute each device's actual local interface address.
Both devices must be on a link that carries IPv4 multicast. Where required,
allow discovery traffic to UDP port `47653` and TCP traffic to Device B's
listen port `47651`.

On Device B, use the verified fingerprint of Device A and start a TLS
listener with discovery enabled.

```sh
PEER_PIN='paste-device-a-verified-fingerprint-here'
./build/connect/textlt_connect_loopback receive-discovery 192.168.1.42 47651 \
  device-b 'Device B' device.crt device.key device-a "$PEER_PIN" accept
```

On Device A, list nearby advertisers. The demo listens for eight seconds
and prints peer IDs, display names, protocol versions, and presence changes.

```sh
./build/connect/textlt_connect_loopback discover 192.168.1.41
```

While Device B is listening, use its verified fingerprint to send by peer ID.
The sender resolves Device B's address and port from discovery. Device A's
command takes its **own** interface address.

```sh
PEER_PIN='paste-device-b-verified-fingerprint-here'
./build/connect/textlt_connect_loopback send-discovered 192.168.1.41 device-b \
  device.crt device.key "$PEER_PIN" 'Hello through discovery'
```

Device A reports `Online device-b (Device B), protocol 1` and then
`Delivered message-1`. Device B prints the offer, message, and acknowledgment.
To verify rejection, restart Device B with an incorrect 64-digit hex
fingerprint for Device A, then repeat the same send command. Device A still
finds Device B, and Device B reports a certificate mismatch before any offer.
Restore the verified fingerprint afterward.

Discovery is optional. The manual IP commands and plain loopback demo
continue to work. For a one-host check, run both devices' commands on the
same machine with separate credentials and IDs, and use the same
non-loopback interface IP in both command sets.

See the [TextLT discovery protocol](../docs/connect-architecture.md#textlt-lan-discovery)
for discovery and trust semantics. Folder transfers and additional structured types are planned for later milestones.
