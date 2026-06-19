<#
.SYNOPSIS
  Register the RivePeek preview handler so Windows Explorer previews .riv files.

.DESCRIPTION
  Runs regsvr32 against RivePeek.dll, which calls DllRegisterServer to write the
  COM class, the .riv association, and the approved-preview-handlers entry under
  HKEY_LOCAL_MACHINE. That requires administrator rights, so this script
  self-elevates if needed.

.PARAMETER Dll
  Path to RivePeek.dll. Defaults to ..\build\bin\RivePeek.dll relative to this script.
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
    Write-Host "Elevating to administrator..."
    Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"", "-Dll", "`"$Dll`""
    )
    return
}

Write-Host "Registering $Dll ..."
$p = Start-Process regsvr32.exe -ArgumentList "/s `"$Dll`"" -Wait -PassThru
if ($p.ExitCode -ne 0) {
    Write-Error "regsvr32 failed with exit code $($p.ExitCode)"
    exit $p.ExitCode
}

# Nudge the Shell so it picks up the new association immediately.
Write-Host "Restarting Explorer to refresh associations..."
Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue)) {
    Start-Process explorer.exe
}

Write-Host "Done. Select a .riv file in Explorer with the Preview Pane (Alt+P) enabled."
