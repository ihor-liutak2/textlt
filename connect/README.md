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

The **Connect** menu is displayed while this workspace is selected. Its
entries open tabs of the same settings modal.

| Menu path | Settings |
| --- | --- |
| **Connect → Connections…** | Choose a discovered peer ID or enter a manual peer IPv4 address and port. Enter the expected SHA-256 fingerprint after verifying it independently. |
| **Connect → Network…** | Enter the local listener address and port, optionally enable LAN discovery, and explicitly **Start** or **Stop** networking. A blank listener port supports sender-only or discovery-only use. |
| **Connect → Identity…** | Set the advertised peer ID and display name, choose local certificate and private key PEM paths, and read or copy the certificate fingerprint. |
| **Connect → Diagnostics…** | Inspect assigned IPv4 and IPv6 addresses, default gateways, and LocalSend listener state. Select an address to copy it or configure LocalSend. |

The existing provider uses one configured peer fingerprint at a time. To
browse first, start discovery with a LAN interface and a blank listen port.
Select a peer under Connections, enter the verified fingerprint and local
certificate and key, then stop and start networking to apply the changes.
To advertise a listener, use the same LAN address for the listener and the
discovery interface and set a local peer ID and display name. Manual
certificate setup and the two-device commands below use the same trust model
as the editor.

Connect Settings automatically saves edits to `connect-settings.json` beside
the editor configuration. The Network tab shows the detected non-loopback
IPv4 address and uses it as the initial listen address. Enter the target
device's IPv4 address in Connections when connecting manually. Conversations
and drafts stay in the current editor session.
Networking stays active when switching workspaces and stops on application
exit. The workspace uses its own Connections column. Its FTXUI UI lives in
`connect/ui/`, and the settings modal is in `src/modals/`. The editor build
links the standalone Connect library and needs the OpenSSL development
dependency listed above.

## Send a file from the editor

1. Set up a trusted TextLT connection on both instances using the certificate
   steps below. Start networking in **Connect → Network…** on both ends.
2. Select the peer in **Options → Connect**. When the peer advertises file
   support or responds to the manual endpoint's TLS capability check,
   **Send File…** appears in the conversation. Choose a regular file in the
   editor's picker.
3. The receiving conversation shows the filename and size. Choose **Accept**
   to save it in Downloads or **Decline** to skip the transfer. Either side
   can use **Cancel transfer** while the file transfer is pending or active.
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

Open **Options → Connect**, then **Connect → Diagnostics…**, or select **Diagnostics** from any Connect Settings tab. The tab lists the
addresses currently assigned to local interfaces, including loopback,
link-local, virtual, and VPN interfaces. It shows the prefix, interface
status, and the type and address origin the OS reports. Linux and macOS
show **Unknown** when their address enumeration provides no origin. Default gateways
appear separately. **Refresh** rereads the current interfaces after a Wi-Fi
change, VPN connection, or other network change. **Copy IP** copies the
selected address without its prefix.

In WSL, **Linux guest (WSL)** and **Windows host** identify the two
environments. The effective networking mode is **Unknown** if WSL cannot
report it. Host data requires Windows PowerShell through WSL interop. If
interop is unavailable, the guest addresses remain visible. **Host only**
means the Windows address is unavailable to the Linux process.

To choose an interface, select an eligible local IPv4 row and click
**Use for LocalSend**. This saves the manual override. Apply it with
**Stop** and **Start** from the LocalSend tab. Clear the interface field to
restore Auto. At Start, Auto looks for an active non-loopback IPv4 address
and prefers non-link-local addresses, active Wi-Fi or Ethernet, and
interfaces with a default route. Diagnostics marks the address selected at Start.

Diagnostics reports configured and actual UDP discovery and TCP/HTTPS
ports separately. For example, set HTTPS to `0` and start LocalSend to see
the port assigned by the operating system. Bind and multicast results
describe the most recent Start attempt and clear on Stop. Actual ports are
shown while the sockets are open. Peer reachability starts as **Unverified** and changes
only after verified HTTPS traffic with a non-local peer during the current
session. Opening or refreshing the tab makes no network probe.

## Exchange text and files with LocalSend

1. Open **Options → Connect**, then **Connect → Network…**, and select the
   **LocalSend** tab. Enable LocalSend and set an alias. The default multicast
   group is `224.0.0.167`, the default discovery UDP port is `53317`, and the default HTTPS receive port is
   `53317`. Leave the interface blank for Auto, or select an eligible IPv4
   address in Diagnostics. Start networking. Connect
   Settings reports bind conflicts. If another app on the same host already
   owns TCP port `53317`, choose a different HTTPS port for TextLT and keep
   the same discovery group and UDP port in both apps.
2. In the official LocalSend app, enable HTTPS and start its receive service.
   The two applications should appear in each other's device list. In
   TextLT, select the peer under **LocalSend** in Connections.
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
