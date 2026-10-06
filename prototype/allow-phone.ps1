# Lets the phone reach audiomic: marks the networks you're connected to right now
# (e.g. the phone's hotspot / USB tethering) as Private, and allows audiomic's port
# on Private networks only. Public networks stay fully blocked.
# Run it once per network: once on hotspot, once on USB tethering.

param([int]$Port = 8443)

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Port $Port"
    exit
}

foreach ($p in Get-NetConnectionProfile | Where-Object NetworkCategory -eq 'Public') {
    Set-NetConnectionProfile -InterfaceIndex $p.InterfaceIndex -NetworkCategory Private
    Write-Host "Marked '$($p.Name)' ($($p.InterfaceAlias)) as Private"
}

Get-NetFirewallRule -DisplayName 'audiomic' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
New-NetFirewallRule -DisplayName 'audiomic' -Direction Inbound -Action Allow -Protocol TCP `
    -LocalPort $Port -Profile Private | Out-Null
Write-Host "Allowed TCP $Port on Private networks"

Read-Host "Done. Press Enter to close"
