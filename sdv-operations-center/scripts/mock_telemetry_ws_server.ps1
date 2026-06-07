#Requires -Version 5.1
# Minimal WebSocket mock for sdv_ros_bridge (port 9090). No extra packages.
$ErrorActionPreference = "Stop"

$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:9090/")
$listener.Start()
Write-Host "Mock bridge listening on ws://127.0.0.1:9090 (HTTP upgrade path /)"

function Send-Text($stream, [string]$text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    $frame = New-Object byte[] ($bytes.Length + 2)
    $frame[0] = 0x81  # FIN + text
    $frame[1] = [byte]$bytes.Length
    [Array]::Copy($bytes, 0, $frame, 2, $bytes.Length)
    $stream.Write($frame, 0, $frame.Length)
    $stream.Flush()
}

function Read-HttpRequest($stream) {
    $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::ASCII, $false, 4096, $true)
    $req = ""
    while ($true) {
        $line = $reader.ReadLine()
        if ($line -eq $null -or $line -eq "") { break }
        $req += $line + "`n"
    }
    return $req
}

function Accept-Key([string]$key) {
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    $accept = $sha1.ComputeHash([System.Text.Encoding]::ASCII.GetBytes($key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
    return [Convert]::ToBase64String($accept)
}

$t0 = Get-Date
while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    $req = $ctx.Request
    if ($req.HttpMethod -eq "GET" -and $req.Headers["Upgrade"] -eq "websocket") {
        $key = $req.Headers["Sec-WebSocket-Key"]
        $accept = Accept-Key $key
        $sw = $ctx.Response.OutputStream
        $writer = New-Object System.IO.StreamWriter($sw, [System.Text.Encoding]::ASCII)
        $writer.Write("HTTP/1.1 101 Switching Protocols`r`n")
        $writer.Write("Upgrade: websocket`r`n")
        $writer.Write("Connection: Upgrade`r`n")
        $writer.Write("Sec-WebSocket-Accept: $accept`r`n`r`n")
        $writer.Flush()

        $hello = '{"schema_version":1,"type":"bridge_hello","data":{"service":"mock-ps"}}'
        Send-Text $sw $hello

        while ($true) {
            $t = ((Get-Date) - $t0).TotalSeconds
            $stamp = [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() * 1000000)
            $lon = 12.0 + 5.0 * [Math]::Sin($t * 0.7)
            $x = 100.0 + 20.0 * [Math]::Cos($t * 0.5)
            $y = 200.0 + 20.0 * [Math]::Sin($t * 0.5)
            $veh = "{`"schema_version`":1,`"type`":`"vehicle_state`",`"stamp_ns`":$stamp,`"frame_id`":`"base_link`",`"data`":{`"longitudinal_velocity_mps`":$lon,`"speed_mps`":$([Math]::Abs($lon))}}"
            $ego = "{`"schema_version`":1,`"type`":`"ego_pose`",`"stamp_ns`":$stamp,`"frame_id`":`"map`",`"data`":{`"position`":{`"x`":$x,`"y`":$y,`"z`":0.5},`"orientation`":{`"w`":1,`"x`":0,`"y`":0,`"z`":0},`"yaw_rad`":0.1,`"speed_mps`":$([Math]::Abs($lon))}}"
            Send-Text $sw $veh
            Send-Text $sw $ego
            Start-Sleep -Milliseconds 200
        }
    } else {
        $ctx.Response.StatusCode = 400
        $ctx.Response.Close()
    }
}
