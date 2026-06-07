$base = "http://127.0.0.1:8080"
$car = "car_1"

function Show($label, $obj) {
  Write-Host "`n=== $label ===" -ForegroundColor Cyan
  if ($obj -is [string]) { Write-Host $obj; return }
  $obj | ConvertTo-Json -Depth 8
}

# 1. GET current safety
$r1 = Invoke-RestMethod "$base/api/fleet/safety/$car?feature=ADAS"
Show "1) GET safety (current)" @{
  ready = $r1.ready
  source = $r1.source_label
  passed = ($r1.checks | Where-Object { $_.passed }).Count
  failed = ($r1.checks | Where-Object { -not $_.passed }).Count
  failed_keys = $r1.failed_checks
}

# 2. Make NOT READY (speed=12)
$r2 = Invoke-RestMethod -Method POST -Uri "$base/api/fleet/safety/$car/update" -ContentType "application/json" -Body '{"speed":12,"source":"manual-demo","feature":"ADAS"}'
Show "2) Set speed=12 (NOT READY)" @{
  ready = $r2.ready
  failed_checks = $r2.failed_checks
  speed_check = ($r2.checks | Where-Object { $_.key -eq "speed" })
}

# 3. assert-ready should fail
try {
  Invoke-RestMethod -Method POST -Uri "$base/api/fleet/safety/$car/assert-ready" -ContentType "application/json" -Body '{"action":"candidate-deploy","feature":"ADAS"}' | Out-Null
  Show "3) assert-ready" "UNEXPECTED: should have failed"
} catch {
  $body = $_.ErrorDetails.Message | ConvertFrom-Json
  Show "3) assert-ready blocked (409)" @{
    ok = $body.ok
    message = $body.message
    failed_checks = $body.failed_checks
  }
}

# 4. Prepare Demo READY
$r4 = Invoke-RestMethod -Method POST -Uri "$base/api/fleet/safety/$car/prepare-demo-ready" -ContentType "application/json" -Body '{"action":"candidate-deploy","feature":"ADAS"}'
Show "4) Prepare Demo READY" @{
  ready = $r4.ready
  source = $r4.source_label
  speed = $r4.snapshot.values.speed
  gear = $r4.snapshot.values.gear
  driver_approved = $r4.snapshot.values.driver_approved
}

# 5. assert-ready should pass
$r5 = Invoke-RestMethod -Method POST -Uri "$base/api/fleet/safety/$car/assert-ready" -ContentType "application/json" -Body '{"action":"candidate-deploy","feature":"ADAS"}'
Show "5) assert-ready OK" @{
  ok = $r5.ok
  ready = $r5.ready
  message = $r5.message
}

Write-Host "`nAll safety API tests completed." -ForegroundColor Green
