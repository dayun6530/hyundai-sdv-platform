# Free SDV bridge WebSocket ports (9090 telemetry, 9092 snapshot, 9093 control).
$ErrorActionPreference = "SilentlyContinue"
$ports = @(9090, 9092, 9093)
$killed = @()

foreach ($port in $ports) {
    $lines = netstat -ano | findstr ":$port " | findstr "LISTENING"
    foreach ($line in $lines) {
        $parts = ($line -replace '\s+', ' ').Trim().Split(' ')
        $pid = $parts[-1]
        if ($pid -match '^\d+$' -and $killed -notcontains $pid) {
            $proc = Get-Process -Id ([int]$pid) -ErrorAction SilentlyContinue
            $name = if ($proc) { $proc.ProcessName } else { "?" }
            Write-Host "Stopping PID $pid ($name) on port $port"
            taskkill /PID $pid /F | Out-Null
            $killed += $pid
        }
    }
}

if ($killed.Count -eq 0) {
    Write-Host "No listeners on 9090/9092/9093"
} else {
    Write-Host "Done. Run: python pygame_map_bridge.py"
}
