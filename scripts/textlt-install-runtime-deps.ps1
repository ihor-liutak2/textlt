$ErrorActionPreference = "Stop"

$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($curl) {
    Write-Host "TextLT runtime dependency check: curl.exe is installed."
    exit 0
}

Write-Host "TextLT needs the external curl.exe executable for cloud/HTTP features."

$winget = Get-Command winget.exe -ErrorAction SilentlyContinue
if ($winget) {
    Write-Host "Installing curl with winget..."
    winget install --id cURL.cURL --source winget --accept-source-agreements --accept-package-agreements
} else {
    Write-Error "curl.exe is missing and winget is not available. Install curl manually or enable the Windows built-in curl.exe feature."
    exit 127
}

$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if (-not $curl) {
    Write-Error "curl installation finished, but curl.exe is still not available in PATH."
    exit 127
}

Write-Host "TextLT runtime dependency check: curl.exe is installed."
