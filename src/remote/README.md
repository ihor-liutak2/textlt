# TextLT remote files

The remote module keeps all remote-file code in `src/remote/`.

## Connection types in the configuration modal

The connection modal has real type switching and separate fields for each supported remote target:

- SFTP / SSH
  - host
  - port
  - user
  - remote root
  - identity file
  - SSH config host alias
- Google Drive
  - account label
  - OAuth client id
  - OAuth client secret
  - token file
  - root folder id
- Microsoft OneDrive / SharePoint
  - account label
  - tenant id
  - OAuth client id
  - OAuth client secret
  - token file
  - SharePoint site id
  - drive id
  - remote root
- Dropbox
  - account label
  - app key
  - app secret
  - token file
  - remote root

All fields are persisted in `remote_connections.json`.

## Active file-manager backend

SFTP is the active file-manager backend in this version. It uses the external `ssh` and `sftp` programs that are already installed on the system.

Google Drive, Microsoft OneDrive/SharePoint, and Dropbox can be configured and saved now. Their actual list/upload/download/delete providers should be implemented in a later patch through the existing `CurlManager`/REST API layer, not through a new HTTP dependency.

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

## SSH/SFTP assumptions

Passwords are not stored. Use one of these:

- SSH key with `ssh-agent`.
- Host alias from `~/.ssh/config`.
- Identity file configured in the connection modal.

The SFTP backend uses batch mode and `BatchMode=yes`, so interactive password prompts are intentionally not supported.

## Patch 8 safety behavior

Copy operations are safer now:

- Uploading from Local to Remote checks whether the selected name already exists in the current remote panel.
- Downloading from Remote to Local checks whether the local destination path already exists.
- If a target exists, TextLT does not overwrite immediately. The operation asks the user to type `OVERWRITE` and press Confirm.
- Delete still requires typing `DELETE` and now shows the exact path being deleted in the status line.

The SFTP provider also checks for external `ssh` and `sftp` executables during connection setup and returns a clear error if they are missing. Paths containing newlines are rejected before building SFTP batch commands, because they cannot be safely represented in the current external `sftp -b -` command mode.

## OAuth token files

Google Drive, Microsoft OneDrive/SharePoint, and Dropbox now share a small token-file layer:

- token files live under `~/.config/textlt/remote_tokens/` by default on Linux;
- the connection modal can create a placeholder token JSON with the `Token` button;
- `Test` for cloud connections validates the configured token file and reports whether it is only a placeholder or already contains an access/refresh token;
- the connection config keeps the token-file path, but the future access/refresh tokens are stored in the token file itself.

This patch does not perform OAuth login yet. The next cloud-provider patches should fill these token files through browser/device login and then use them from the Google Drive, Microsoft Graph, and Dropbox REST providers.

## Patch 10 Dropbox backend

Dropbox is now the first active cloud file-manager backend.

The Dropbox provider uses the existing project libcurl dependency and Dropbox HTTP API endpoints. It does not add a new HTTP library.

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

Google Drive is active after patch 11. Microsoft OneDrive/SharePoint is still configuration-only and should follow the same `IRemoteProvider` pattern in a later patch.

## Patch 11 Google Drive backend

Google Drive is now an active cloud file-manager backend alongside SFTP and Dropbox.

The Google Drive provider uses the existing project libcurl dependency and Google Drive API v3 endpoints. It does not add a new HTTP library.

Supported Google Drive operations:

- list folder;
- download ordinary binary files with `alt=media`;
- upload new files with multipart upload;
- overwrite existing ordinary files with media upload;
- recursive folder download;
- recursive folder upload;
- rename files or folders;
- delete files or folders;
- create folders;
- open remote file through the same local cache used by SFTP and Dropbox;
- manual `Sync Last` upload for cached Google Drive files.

Google Drive root path is represented as `/` in TextLT. If `Root folder ID` is empty, the provider uses the Drive `root` alias. If `Root folder ID` is set, `/` in the TextLT remote panel maps to that configured folder.

To use Google Drive files before a full OAuth login modal exists, the configured token file must contain an `access_token` value. The placeholder token created by the connection modal is not enough by itself.

Google Workspace native files such as Docs, Sheets, and Slides are shown as non-ordinary entries. They can be renamed or deleted, but download/export support is intentionally left for a later patch.

Microsoft OneDrive/SharePoint remains configuration-only at this step.
