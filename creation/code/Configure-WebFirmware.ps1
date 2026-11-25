param(
  [Parameter(Mandatory)]
  [Uri]$AppUrl
)

$ErrorActionPreference = 'Stop'
if (!$AppUrl.IsAbsoluteUri -or $AppUrl.Scheme -ne 'https' -or $AppUrl.AbsolutePath -ne '/' -or
    $AppUrl.UserInfo -or $AppUrl.Query -or $AppUrl.Fragment) {
  throw 'Enter the app HTTPS address, such as https://i-shoot-mo.vercel.app.'
}

function ConvertTo-ArduinoString([string]$Value) {
  $escaped = [Text.Encoding]::UTF8.GetBytes($Value) | ForEach-Object { '\x{0:x2}' -f $_ }
  return '"' + ($escaped -join '') + '"'
}

$credentialPath = Join-Path $PSScriptRoot '../../web-app/.env.deployment.local'
if (Test-Path -LiteralPath $credentialPath) {
  $settings = Get-Content -LiteralPath $credentialPath -Raw | ConvertFrom-StringData
  $deviceToken = $settings['DEVICE_TOKEN']
} else {
  $deviceToken = [Net.NetworkCredential]::new('', (Read-Host 'Board token' -AsSecureString)).Password
}
if (!$deviceToken -or $deviceToken.Length -lt 32) {
  throw 'The board token must match DEVICE_TOKEN on Vercel.'
}

$network = Read-Host '2.4 GHz Wi-Fi name'
$password = [Net.NetworkCredential]::new('', (Read-Host 'Wi-Fi password (hidden)' -AsSecureString)).Password
if ([Text.Encoding]::UTF8.GetByteCount($network) -notin 1..32) {
  throw 'Enter a Wi-Fi name that is 1 to 32 bytes long.'
}
if ($password -and [Text.Encoding]::UTF8.GetByteCount($password) -notin 8..63 -and
    $password -notmatch '^[a-fA-F0-9]{64}$') {
  throw 'Enter the Wi-Fi network password.'
}

$config = @(
  '#pragma once'
  ('#define ISHOOT_WIFI_NAME ' + (ConvertTo-ArduinoString $network))
  ('#define ISHOOT_WIFI_PASSWORD ' + (ConvertTo-ArduinoString $password))
  ('#define ISHOOT_APP_URL ' + (ConvertTo-ArduinoString $AppUrl.GetLeftPart([UriPartial]::Authority)))
  ('#define ISHOOT_DEVICE_TOKEN ' + (ConvertTo-ArduinoString $deviceToken))
)
$destination = Join-Path $PSScriptRoot 'ishootmo_code/online_config.h'
[IO.File]::WriteAllLines($destination, $config, [Text.UTF8Encoding]::new($false))
Write-Host 'Board settings saved. The online sketch is ready to verify and upload.'
