# Set up IRC in TextLT

1. Open **Options → Connect** and choose **IRC Settings** under **Messengers**.
2. Set a connection name, nickname and username. The default connection uses
   `irc.libera.chat:6697` with TLS enabled.
3. If your network uses SASL, enter the SASL account and password and leave
   TLS enabled. Use **Server password** only for a password required by the
   IRC server itself. Plain TCP transmits the server password unencrypted.
4. Enter comma- or space-separated channels such as `#example, #another`.
   **Automatically reconnect** retries dropped connections and rejoins channels
   that have been kept open. **Start on application launch** connects using this
   saved profile each time TextLT starts.
5. Choose **Test Connection** to check DNS, the connection, certificate verification
   for TLS, and IRC registration. The test uses a separate connection, so stop
   an existing connection first if it already uses the same nickname.
6. Choose **Save**. Select the server in **Messengers** and choose **Start**.
   Use **New** in IRC Settings to add another server.

Libera.Chat can require SASL for connections from some networks. If the server
reports that requirement, configure a NickServ account with a verified email
address in **SASL account** and **SASL password**.

Select a server in **Messengers**, then use the channel/nickname field and
**Join Channel** or **Open Private Chat** in the IRC navigation footer. Select
the new entry in **Messengers** to chat. Use the compact **Leave** action on a
joined channel row and **Stop** beside the selected server to disconnect.
Saved channels and channels joined during the session rejoin after a disconnect,
except channels you explicitly left.
Conversation history and channels added during a session are kept in memory.

IRC Settings saves masked credentials in plaintext in `irc-settings.json`,
beside the main TextLT configuration. On POSIX, the settings file is written
with owner-only permissions. The default locations are
`~/.config/textlt/irc-settings.json` on Linux and macOS and
`/state/.config/textlt/irc-settings.json` in Docker. On Windows, use the TextLT
directory under `%APPDATA%`.

TLS verifies the server name and certificate chain using OpenSSL's trust
store. Install current CA certificates if verification fails. Older Windows
OpenSSL builds can use a PEM trust bundle specified through `SSL_CERT_FILE`.
IRC provides text chat. Sending files and DCC are outside this provider's scope.
