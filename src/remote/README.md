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
