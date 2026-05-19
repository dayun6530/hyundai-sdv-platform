#Requires -Version 5.1
<#
.SYNOPSIS
  Diagnose why Vite cannot bind to 5170/5173/5174/5175 on Windows (EACCES / Hyper-V excluded ranges).

.DESCRIPTION
  Team default UI ports (do not change without team agreement):
    admin-portal  5170
    dashboard     5173
    test-ui       5174
    sdv-cockpit-ui 5175

  Common root cause on Windows + Docker Desktop/WSL:
    Hyper-V / WinNAT reserves dynamic TCP ranges that sometimes cover 5105-5204,
    which blocks binding with "EACCES: permission denied" (not fixable by killing node.exe).

.PARAMETER FixHint
  Print remediation steps when a port is blocked.

.PARAMETER Quiet
  Only write on failure; exit 0 if all four ports are listenable, else 1.
#>
param(
    [switch]$FixHint,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$Ports = @(5170, 5173, 5174, 5175)

function Write-Info([string]$Message) {
    if (-not $Quiet) {
        Write-Host $Message
    }
}

function Test-PortListenable([int]$Port) {
    $listener = $null
    try {
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($null -ne $listener) {
            try { $listener.Stop() } catch { }
        }
    }
}

function Get-ExcludedTcpRanges() {
    $ranges = @()
    $lines = netsh interface ipv4 show excludedportrange protocol=tcp 2>$null
    if (-not $lines) {
        return $ranges
    }
    foreach ($line in $lines) {
        if ($line -match '^\s*(\d+)\s+(\d+)\s+') {
            $ranges += [PSCustomObject]@{
                Start = [int]$Matches[1]
                End   = [int]$Matches[2]
            }
        }
    }
    return $ranges
}

function Test-PortInExcludedRange([int]$Port, $Ranges) {
    foreach ($r in $Ranges) {
        if ($Port -ge $r.Start -and $Port -le $r.End) {
            return $r
        }
    }
    return $null
}

function Show-FixSteps {
    Write-Host ""
    Write-Host "=== Remediation (try in order; admin PowerShell for step 2) ===" -ForegroundColor Yellow
    Write-Host "1. Close old Vite CMD windows; run_sdv.ps1 already stops listeners on 5170, 5173-5175."
    Write-Host "2. Restart Docker Desktop (WSL integration on), then reboot Windows if still blocked."
    Write-Host "3. Admin:  net stop winnat"
    Write-Host "          net start winnat"
    Write-Host "   (Resets dynamic port exclusions until next Hyper-V/Docker change; reboot if unsure.)"
    Write-Host "4. Re-run:  .\scripts\diagnose_vite_ports.ps1"
    Write-Host "5. Linux/macOS teammates are unaffected; this is a Windows host reservation issue."
    Write-Host ""
}

$excluded = Get-ExcludedTcpRanges
$blocked = @()

Write-Info "SDV Vite port check (team defaults: 5170, 5173, 5174, 5175)"
if ($excluded.Count -gt 0) {
    Write-Info "Windows excluded TCP ranges (netsh):"
    foreach ($r in $excluded) {
        Write-Info ("  {0} - {1}" -f $r.Start, $r.End)
    }
} else {
    Write-Info "Could not read excluded port ranges (netsh)."
}

Write-Info ""
foreach ($p in $Ports) {
    $inEx = Test-PortInExcludedRange -Port $p -Ranges $excluded
    $canListen = Test-PortListenable -Port $p
    if ($canListen) {
        Write-Info ("  Port {0}: OK (bind test succeeded)" -f $p)
    } else {
        $blocked += $p
        $detail = if ($inEx) {
            "inside excluded range {0}-{1}" -f $inEx.Start, $inEx.End
        } else {
            "bind failed (in use or reserved)"
        }
        Write-Host ("  Port {0}: BLOCKED ({1})" -f $p, $detail) -ForegroundColor Red
    }
}

if ($blocked.Count -eq 0) {
    Write-Info "All Vite ports are available."
    exit 0
}

Write-Host ""
Write-Host ("Blocked port(s): {0}" -f ($blocked -join ', ')) -ForegroundColor Red
Write-Host "Vite will show: EACCES permission denied 0.0.0.0:<port>" -ForegroundColor Red
Write-Host "This is NOT fixed by killing node.exe alone when the range is OS-reserved." -ForegroundColor Red

if ($FixHint -or -not $Quiet) {
    Show-FixSteps
}

exit 1
