param(
    [string]$Version = $(if ($env:TEXTLT_VERSION) { $env:TEXTLT_VERSION } else { "" })
)

$ErrorActionPreference = "Stop"

if (-not $Version) {
    Write-Host "Detecting latest version from GitHub..."
    try {
        $releaseInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/ihor-liutak2/textlt/releases/latest"
        $Version = $releaseInfo.tag_name
        Write-Host "Auto-detected latest version: $Version"
    } catch {
        throw "Could not detect latest version. Pass it as argument: -Version vX.Y.Z"
    }
}
if (-not $Version.StartsWith("v")) {
    $Version = "v$Version"
}

$Repo = if ($env:TEXTLT_REPO) { $env:TEXTLT_REPO } else { "ihor-liutak2/textlt" }
$Asset = if ($env:TEXTLT_ASSET) { $env:TEXTLT_ASSET } else { "textlt-windows-x64.zip" }
$InstallRoot = if ($env:TEXTLT_INSTALL_ROOT) { $env:TEXTLT_INSTALL_ROOT } else { Join-Path $env:LOCALAPPDATA "Programs\textlt" }
$AppDir = Join-Path $InstallRoot "app"
$ConfigDir = Join-Path $env:APPDATA "textlt"
$LegacyConfigFile = Join-Path $env:USERPROFILE ".textlt_config.json"
$Archive = Join-Path $env:TEMP $Asset
$ExtractDir = Join-Path $env:TEMP ("textlt-install-" + [guid]::NewGuid().ToString("N"))
$Url = if ($env:TEXTLT_DOWNLOAD_URL) { $env:TEXTLT_DOWNLOAD_URL } else { "https://github.com/$Repo/releases/download/$Version/$Asset" }

New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null

try {
    Write-Host "Downloading TextLT $Version for Windows..."
    Invoke-WebRequest -Uri $Url -OutFile $Archive

    Write-Host "Extracting archive..."
    Expand-Archive -Force -Path $Archive -DestinationPath $ExtractDir

    $Exe = Get-ChildItem $ExtractDir -Recurse -Filter textlt.exe | Select-Object -First 1
    if (-not $Exe) {
        throw "textlt.exe was not found in the extracted archive."
    }

    $ExeDir = Split-Path $Exe.FullName

    Write-Host "Resetting TextLT configuration and bundled resources..."
    Remove-Item $ConfigDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item $LegacyConfigFile -Force -ErrorAction SilentlyContinue

    # Text parser configuration used XDG_CONFIG_HOME/HOME in some releases,
    # including on Windows. Clear that exact TextLT directory when present.
    if ($env:XDG_CONFIG_HOME) {
        $XdgConfigDir = Join-Path $env:XDG_CONFIG_HOME "textlt"
        if ($XdgConfigDir -ne $ConfigDir) {
            Remove-Item $XdgConfigDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    } elseif ($env:HOME) {
        $HomeConfigDir = Join-Path $env:HOME ".config\textlt"
        if ($HomeConfigDir -ne $ConfigDir) {
            Remove-Item $HomeConfigDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    # Remove application-managed leftovers from older releases. Installed
    # Piper voices and AI/llama.cpp data use the piper and ai directories;
    # those directories are deliberately not removed.
    @("themes", "text_processors", "resources", "registries") | ForEach-Object {
        Remove-Item (Join-Path $InstallRoot $_) -Recurse -Force -ErrorAction SilentlyContinue
    }

    Remove-Item $AppDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
    Copy-Item -Path (Join-Path $ExeDir "*") -Destination $AppDir -Recurse -Force

    $CmdPath = Join-Path $InstallRoot "textlt.cmd"
    @"
@echo off
"%~dp0app\textlt.exe" %*
"@ | Set-Content -Encoding ASCII $CmdPath

    $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $PathParts = @()
    if ($UserPath) {
        $PathParts = $UserPath -split ';' | Where-Object { $_ }
    }
    if ($PathParts -notcontains $InstallRoot) {
        $NewPath = (@($PathParts) + $InstallRoot) -join ';'
        [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
    }

    Write-Host ""
    Write-Host "TextLT $Version installed to: $AppDir"
    Write-Host "Open a new PowerShell window and run: textlt"
} finally {
    Remove-Item $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue
}
