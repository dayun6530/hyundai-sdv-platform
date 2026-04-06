# External signals sample for SignalReceiver (prototype).
# Format: <type>:<target>:<payload>
# The SignalReceiver reads config/ExternalSignals.cmd, writes to logs/SignalsInbound.cmd.
# FeatureEvaluator consumes logs/SignalsInbound.cmd and maps some types to actions.

# Demo canonical situation types (recommended for presentation)
# Format: <type>:<target>:<payload>

# Driver fell asleep (Driver Monitoring System)
DMS_SLEEP:AdasFeature:driver_eyes_closed_3s

# Minimal demo payload (copy/paste friendly)
DMS_SLEEP:AdasFeature:demo

# Driver sleep (generic)
DRIVER_SLEEP:AdasFeature:driver_not_responding   # alias -> DMS_SLEEP

# ADAS fatal error (e.g., critical sensor failure)
ADAS_FATAL:AdasFeature:lidar_timeout

# Sensor / input fatal error
SENSOR_FATAL:AdasFeature:camera_disconnected

# Manual kill (operator)
MANUAL_KILL:AdasFeature:demo

# A/B (third field = payload): A와 B를 동시에 켤 수 있음 — SignalReceiver·직접 SignalsInbound 동일 형식
FLAG_ON:AdasFeature:A
FLAG_ON:AdasFeature:B
FLAG_ON:EvEnergyFeature:A
FLAG_ON:EvEnergyFeature:B
FLAG_ON:NavigationFeature:A
FLAG_ON:NavigationFeature:B

