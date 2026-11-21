# TextLT remote files

The remote module keeps all remote-file code in `src/remote/`.

## Connection types in the configuration modal

The connection modal has real type switching and separate fields for each supported remote target:

- SFTP
  - local connection name
  - concrete Host alias discovered from `~/.ssh/config`
  - remote root
- FTPS
  - host, port, username, and password
  - remote root
  - explicit or implicit TLS
  - passive or active data mode
  - automatic certificate acceptance
- Dropbox
  - app key
  - app secret
  - token file
  - access token input
  - refresh token input
  - remote root

Connection name/type and provider settings are persisted in `remote_connections.json`; pasted access tokens are stored in separate token files.

## Active file-manager backends

SFTP, FTPS, and Dropbox are active file-manager backends in this version. SFTP delegates connection resolution to OpenSSH config and uses the external `sftp` program for remote file operations. FTPS and Dropbox use the external `curl` executable; no curl development library is linked.

## Supported for SFTP

- SFTP connection configuration.
- Two-panel local/remote file manager.
- Directory navigation by Enter or double click.
- File and directory copy in both directions.
- Rename, mkdir, and delete with confirmation.
- Opening a remote file through a temporary local cache.
- Manual upload of the last cached remote file with `Sync Last`.

## Not automatic by design

Saving a cached remote file in the editor does not upload it back automatically. The safe workflow is:

```text
Open remote file
Edit
Save local cached copy
Remote Files
Sync Last
```

This avoids accidental writes to servers when the user only wanted to save the local cached copy.

## SFTP and OpenSSH config

SFTP connections select a concrete `Host` alias discovered from `~/.ssh/config` and recursively included config files. Wildcard and negated Host patterns are not shown as selectable connections. OpenSSH resolves the actual host, user, port, identity files, ProxyJump, and host verification. TextLT runs `sftp` in non-interactive batch mode, so authentication should use an SSH key or ssh-agent rather than a password prompt.

## FTPS

FTPS is a separate external-curl backend. Explicit TLS uses `AUTH TLS` and normally port 21; implicit TLS normally uses port 990. Passive mode is enabled by default. Server certificates are accepted automatically, so FTPS traffic is encrypted but server identity is not verified. Credentials are passed to curl through a temporary owner-only configuration file and are not placed in process arguments. Directory browsing supports common Unix and Windows FTP `LIST` formats.

## Patch 8 safety behavior

Copy operations are safer now:

- Uploading from Local to Remote checks whether the selected name already exists in the current remote panel.
- Downloading from Remote to Local checks whether the local destination path already exists.
- If a target exists, TextLT does not overwrite immediately. The operation asks the user to type `OVERWRITE` and press Confirm.
- Delete still requires typing `DELETE` and now shows the exact path being deleted in the status line.

The SFTP provider checks for external `ssh` and `sftp` executables during connection setup, validates the selected alias through `ssh -G`, and returns a clear error if resolution fails. Paths containing newlines are rejected before building SFTP batch commands, because they cannot be safely represented in the current external `sftp -b -` command mode.

## OAuth token files

Dropbox uses a small token-file layer:

- token files live under `~/.config/textlt/remote_tokens/` by default on Linux;
- the connection modal can save pasted access/refresh tokens from the provider tab;
- `Test` for cloud connections validates the configured token file and reports whether it is only a placeholder or already contains an access/refresh token;
- the connection config keeps the token-file path, but the future access/refresh tokens are stored in the token file itself.

This module does not perform OAuth login yet. A later OAuth modal should fill these token files through browser/device login and then the cloud providers use those tokens for REST calls.

## Patch 10 Dropbox backend

Dropbox is now the first active cloud file-manager backend.

The Dropbox provider uses Dropbox HTTP API endpoints through `RemoteHttpClient`, which calls the external `curl` executable. It does not require linking TextLT against a curl development library.

Supported Dropbox operations:

- list folder;
- download file;
- upload file with overwrite mode;
- recursive folder download;
- recursive folder upload;
- rename/move;
- delete file or folder;
- create folder;
- open remote file through the same local cache used by SFTP;
- manual `Sync Last` upload for cached Dropbox files.

Dropbox root path is represented as `/` in TextLT and as an empty path string in the Dropbox API. The provider converts this internally.

To use Dropbox files before a full OAuth login modal exists, the configured token file must contain an `access_token` value. The placeholder token created by the connection modal is not enough by itself.

## External curl runtime dependency

TextLT no longer links against a curl development library for the remote/cloud module. HTTP requests are centralized in `src/remote/remote_http_client.*` and executed through the system `curl` program. This keeps the binary/install package smaller when the target system already provides `curl`. Transfers use a 20-second progress/idle window: as long as data continues to move, the operation keeps receiving another window; if no data moves for 20 seconds, curl reports a stalled transfer.

Install the runtime tool on Debian/Ubuntu/MX Linux with:

```bash
sudo apt install curl
```

The older `CurlManager` also uses the same external-curl path now, so the curl development package is no longer required for building TextLT.

## Runtime dependency closure

TextLT does not link against a curl development library for remote/cloud features.
HTTP requests are sent through the external `curl` executable, and SFTP/SSH
operations use the external OpenSSH tools `ssh` and `sftp`.

Required runtime tools for remote work:

```bash
curl
ssh
sftp
```

Linux packages:

```bash
# Debian / Ubuntu / MX Linux
sudo apt install curl openssh-client

# Fedora
sudo dnf install curl openssh-clients

# Arch
sudo pacman -S curl openssh

# Alpine
sudo apk add curl openssh-client
```

The release package includes:

```text
textlt-install-runtime-deps.sh
textlt-install-runtime-deps.ps1
```

The Linux launcher checks `curl`, `ssh`, and `sftp` before starting TextLT and
runs the installer script if something is missing.

External `curl` uses a 20-second connection/progress window:

- connection timeout: 20 seconds;
- stalled-transfer timeout: 20 seconds without at least 1 byte/second;
- if data keeps moving, transfer continues through the next 20-second window.
