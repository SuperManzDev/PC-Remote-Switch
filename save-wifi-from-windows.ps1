param(
    [string]$Ssid
)

$ErrorActionPreference = "Stop"

function ConvertTo-CStringLiteral {
    param([string]$Value)

    if ($null -eq $Value) {
        return ""
    }

    return $Value.Replace("\", "\\").Replace('"', '\"')
}

if (-not $Ssid) {
    $interfaces = netsh wlan show interfaces
    $connectedLine = $interfaces | Where-Object { $_ -match '^\s*SSID\s+:\s+' -and $_ -notmatch 'BSSID' } | Select-Object -First 1

    if ($connectedLine -match '^\s*SSID\s+:\s+(.+)$') {
        $Ssid = $Matches[1].Trim()
    }
}

if (-not $Ssid) {
    $profiles = netsh wlan show profiles
    $profileLine = $profiles | Where-Object { $_ -match 'All User Profile\s+:\s+(.+)$' } | Select-Object -First 1

    if ($profileLine -match 'All User Profile\s+:\s+(.+)$') {
        $Ssid = $Matches[1].Trim()
    }
}

if (-not $Ssid) {
    throw "No saved Windows WiFi profile was found. Run this again with: .\save-wifi-from-windows.ps1 -Ssid `"Your WiFi Name`""
}

$profile = netsh wlan show profile name="$Ssid" key=clear
$keyLine = $profile | Where-Object { $_ -match 'Key Content\s+:\s+(.+)$' } | Select-Object -First 1
$password = ""

if ($keyLine -match 'Key Content\s+:\s+(.+)$') {
    $password = $Matches[1].Trim()
}

$includeDir = Join-Path $PSScriptRoot "include"
$outputPath = Join-Path $includeDir "wifi_config.h"
New-Item -ItemType Directory -Force -Path $includeDir | Out-Null

$escapedSsid = ConvertTo-CStringLiteral $Ssid
$escapedPassword = ConvertTo-CStringLiteral $password

@"
#pragma once

#define WIFI_CONFIG_HAS_CREDENTIALS 1
static const char WIFI_CONFIG_SSID[] = "$escapedSsid";
static const char WIFI_CONFIG_PASSWORD[] = "$escapedPassword";
"@ | Set-Content -Path $outputPath -Encoding ASCII

Write-Host "Saved WiFi config for SSID '$Ssid' to $outputPath"
Write-Host "Password was written to include\wifi_config.h for firmware upload. Keep that file private."
