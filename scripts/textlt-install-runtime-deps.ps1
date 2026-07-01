$ErrorActionPreference = "Stop"

$missing = @()
foreach ($tool in @("curl.exe", "ssh.exe", "sftp.exe")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        $missing += $tool
    }
}

if ($missing.Count -eq 0) {
    Write-Host "TextLT runtime dependencies are already installed: curl.exe ssh.exe sftp.exe"
    exit 0
}

Write-Host "TextLT missing runtime dependencies: $($missing -join ', ')"

if ($missing -contains "ssh.exe" -or $missing -contains "sftp.exe") {
    Write-Host "Trying to install OpenSSH Client Windows capability..."
    try {
        Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0 | Out-Null
    } catch {
        Write-Warning "Could not install OpenSSH Client automatically. Enable 'OpenSSH Client' in Windows Optional Features."
    }
}

if ($missing -contains "curl.exe") {
    if (Get-Command winget.exe -ErrorAction SilentlyContinue) {
        Write-Host "Trying to install curl with winget..."
        winget install --id cURL.cURL --silent --accept-package-agreements --accept-source-agreements
    } else {
        Write-Warning "winget is not available. Install curl.exe manually or add it to PATH."
    }
}

$stillMissing = @()
foreach ($tool in @("curl.exe", "ssh.exe", "sftp.exe")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        $stillMissing += $tool
    }
}

if ($stillMissing.Count -gt 0) {
    throw "TextLT runtime dependencies are still missing: $($stillMissing -join ', ')"
}

Write-Host "TextLT runtime dependencies installed: curl.exe ssh.exe sftp.exe"
