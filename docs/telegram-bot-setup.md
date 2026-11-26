# Set up Telegram in TextLT

TextLT connects to Telegram using a bot you control. You can send text
messages and exchange files with authorized chats.

## Create and test a bot

1. In Telegram, open the verified **@BotFather** account and create a bot with
   `/newbot`.
2. Copy the bot token from BotFather. Treat it like a password: anyone with the
   token can control the bot.
3. In TextLT, open **Options → Connect**, then **Messengers → Settings**.
4. Paste the token and choose **Test Connection**. TextLT displays the
   authenticated bot name and username when Telegram accepts the token.
5. Choose **Save**, then use **Start** in the Messengers panel to begin polling.
6. Open the bot in Telegram and send `/start`. TextLT discovers chats only
   from incoming updates; you do not need to find or enter a chat ID.
7. Return to **Messengers → Settings**. Select each discovered chat and press
   Enter to authorize or revoke it, then choose **Save**.
8. Authorized chats appear in the Messengers panel; select one to use the
   shared Connect conversation and composer.

For a group, add the bot to the group and send a message that Telegram delivers
to the bot. Telegram's group privacy settings can restrict which group messages
a bot receives. TextLT cannot discover a group until at least one update from
that group reaches the bot.
If the bot has group privacy enabled, send the attachment as a direct reply
to one of the bot's messages.

## Send and receive files

Select an authorized chat and choose **Send File…**. TextLT uploads files
as Telegram documents, including archives, images and audio. TextLT also
receives Telegram photos, audio, videos and voice messages. Send images
as files in Telegram to preserve their original quality and filenames.

The **Messengers** list marks chats with a pending file offer. TextLT displays
the filename and size when Telegram provides them. Choose
**Accept** to start the download or **Decline** to dismiss it. Accepted files
use the standard Connect Downloads destination. The hosted Telegram Bot API
allows uploads up to 50 MB and downloads up to 20 MB.

## Reconnection and startup

**Automatically reconnect** retries polling after temporary network/server
errors and honors Telegram's `retry_after` rate-limit response. **Start on
application launch** starts polling when TextLT opens if a token is saved.
Otherwise, start and stop Telegram independently from the Messengers panel.

If polling reports a Telegram `getUpdates` conflict, the bot has an active
webhook. Telegram does not permit long polling while a webhook is configured;
remove that webhook through the bot's existing management process, then start
Telegram again.

## Token storage

TextLT writes the token and Telegram settings to a separate
`telegram-settings.json` file in the normal TextLT configuration directory:

- Linux: `~/.config/textlt/telegram-settings.json`
- Docker: `/state/.config/textlt/telegram-settings.json` in the existing
  `textlt-state` volume
- Windows: beside the normal TextLT configuration under `%APPDATA%\textlt`

On POSIX systems the file is written with owner-only permissions. The token is
plaintext at rest, not encrypted; protect the file and any backups. TextLT
redacts the token from connection diagnostics and does not include it in
application log messages.

TextLT uses Telegram's HTTPS Bot API. The bot token is sent only to Telegram
over HTTPS. Do not share the token, paste it into support reports, or include
it in screenshots.
