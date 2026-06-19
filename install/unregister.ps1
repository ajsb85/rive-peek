<#
.SYNOPSIS
  Unregister the RivePeek preview handler (regsvr32 /u, calls DllUnregisterServer).
  Self-elevates because the registry keys live under HKEY_LOCAL_MACHINE.
#>
param(
    [string]$Dll = (Join-Path $PSScriptRoot "..\build\bin\RivePeek.dll")
)

$ErrorActionPreference = "Stop"
$Dll = (Resolve-Path $Dll).Path

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltinRole]::Administrator)
}

if (-not (Test-Admin)) {
    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"", "-Dll", "`"$Dll`""
    )
    return
}

Write-Host "Unregistering $Dll ..."
$p = Start-Process regsvr32.exe -ArgumentList "/u /s `"$Dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) { Write-Error "regsvr32 /u failed: $($p.ExitCode)"; exit $p.ExitCode }

Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
Write-Host "Done."
