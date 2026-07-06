param(
    [string]$Version = $(if ($env:TEXTLT_VERSION) { $env:TEXTLT_VERSION } else { "v0.9.1" })
)

$ErrorActionPreference = "Stop"

$Repo = if ($env:TEXTLT_REPO) { $env:TEXTLT_REPO } else { "ihor-liutak2/textlt" }
$Asset = if ($env:TEXTLT_ASSET) { $env:TEXTLT_ASSET } else { "textlt-windows-x64.zip" }
$InstallRoot = if ($env:TEXTLT_INSTALL_ROOT) { $env:TEXTLT_INSTALL_ROOT } else { Join-Path $env:LOCALAPPDATA "Programs\textlt" }
$AppDir = Join-Path $InstallRoot "app"
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
    Remove-Item $AppDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
    Copy-Item -Path (Join-Path $ExeDir "*") -Destination $AppDir -Recurse -Force

    $CmdPath = Join-Path $InstallRoot "textlt.cmd"
    @"
@echo off
cd /d "%~dp0app"
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

    Write-Host "TextLT installed to: $AppDir"
    Write-Host "Open a new PowerShell window and run: textlt"
} finally {
    Remove-Item $ExtractDir -Recurse -Force -ErrorAction SilentlyContinue
}
