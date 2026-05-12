import { motion } from 'framer-motion'
import { useEffect, useId, useState, type ReactNode } from 'react'
import {
  FiActivity,
  FiBell,
  FiCpu,
  FiHome,
  FiMap,
  FiNavigation,
  FiSettings,
  FiShield,
  FiZap,
} from 'react-icons/fi'
import { create } from 'zustand'

type ViewMode = 'first' | 'third' | 'dashboard' | 'ops' | 'log'

type CockpitStore = {
  vehicleId: string
  viewMode: ViewMode
  setVehicleId: (vehicleId: string) => void
  setViewMode: (viewMode: ViewMode) => void
}

type VehicleTelemetry = {
  speed: number
  battery: number
  rangeKm: number
  routeDistance: string
  routeDirection: string
  locationLabel: string
  roadSummary: string
  speedLimit: number
  connected: boolean
}

const useCockpitStore = create<CockpitStore>((set) => ({
  vehicleId: 'car_1',
  viewMode: 'first',
  setVehicleId: (vehicleId) => set({ vehicleId }),
  setViewMode: (viewMode) => set({ viewMode }),
}))

const navItems = [
  { label: '홈', icon: FiHome },
  { label: '차량', icon: FiActivity },
  { label: '지도', icon: FiMap },
  { label: 'ADAS', icon: FiShield },
  { label: '인포테인', icon: FiNavigation },
  { label: 'EV', icon: FiZap },
  { label: '설정', icon: FiSettings },
]

const viewModes: Array<{ id: ViewMode; label: string }> = [
  { id: 'first', label: '1인칭' },
  { id: 'third', label: '3인칭' },
  { id: 'dashboard', label: '대시보드' },
  { id: 'ops', label: '운영' },
  { id: 'log', label: '로그' },
]

const events = [
  ['14:25:10', 'OTA deployment completed', 'ADAS v3.1.0', 'SUCCESS'],
  ['14:24:55', 'Shadow validation passed', 'ADAS Shadow', 'SUCCESS'],
  ['14:24:40', 'Traffic shifted to 20%', 'A/B Rollout', 'SUCCESS'],
  ['14:24:32', 'Version switched', 'ADAS v3.1.0', 'SUCCESS'],
  ['14:24:20', 'Verification completed', 'ADAS v3.1.0', 'SUCCESS'],
  ['14:23:58', 'Package downloaded', 'ADAS v3.1.0', 'SUCCESS'],
]

type CockpitApiPayload = {
  ok?: boolean
  speed?: number
  battery?: number
  range_km?: number
  route_distance?: string
  route_direction?: string
  location?: { label?: string; lat?: number; lon?: number } | null
  road?: { summary?: string; detail?: string; speed_limit?: number } | null
}

type FleetStatePayload = {
  ok?: boolean
  states?: Array<{
    car_id?: string
    ota?: { speed?: number; battery?: number; gear?: string }
    features?: {
      ADAS?: { active_version?: string; flag?: string }
      Navigation?: { active_version?: string; flag?: string }
      Energy?: { active_version?: string; flag?: string }
    }
  }>
}

function useVehicleTelemetry(vehicleId: string): VehicleTelemetry {
  const [speed, setSpeed] = useState(72)
  const [apiTelemetry, setApiTelemetry] = useState<Partial<VehicleTelemetry>>({})

  useEffect(() => {
    const startedAt = performance.now()
    const timer = window.setInterval(() => {
      const elapsed = (performance.now() - startedAt) / 1000
      const nextSpeed = 72 + Math.sin(elapsed * 0.48) * 8 + Math.sin(elapsed * 1.35) * 3
      setSpeed(Math.round(Math.max(52, Math.min(92, nextSpeed))))
    }, 450)

    return () => window.clearInterval(timer)
  }, [])

  useEffect(() => {
    let alive = true

    async function pollCockpitTelemetry() {
      try {
        const response = await fetch(`http://127.0.0.1:8080/api/fleet/cockpit?car_id=${encodeURIComponent(vehicleId)}`, {
          headers: { Accept: 'application/json' },
        })
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`)
        }
        const payload = (await response.json()) as CockpitApiPayload
        if (!alive || !payload.ok) {
          return
        }
        setApiTelemetry({
          battery: clampPercent(payload.battery, 82),
          connected: true,
          locationLabel: payload.location?.label || payload.route_direction || '판교 / 분당 방면',
          rangeKm: finiteNumber(payload.range_km, 312),
          roadSummary: payload.road?.summary || '직진 주행',
          routeDirection: payload.route_direction || payload.location?.label || '판교 / 분당 방면',
          routeDistance: payload.route_distance || '120 m',
          speed: finiteNumber(payload.speed, 72),
          speedLimit: finiteNumber(payload.road?.speed_limit, 80),
        })
      } catch {
        if (alive) {
          try {
            const response = await fetch('http://127.0.0.1:8080/api/fleet/state', {
              headers: { Accept: 'application/json' },
            })
            const payload = (await response.json()) as FleetStatePayload
            const car = payload.states?.find((state) => state.car_id === vehicleId)
            if (car) {
              setApiTelemetry((prev) => ({
                ...prev,
                battery: clampPercent(car.ota?.battery, 82),
                connected: true,
                rangeKm: Math.round(clampPercent(car.ota?.battery, 82) * 3.8),
                speed: finiteNumber(car.ota?.speed, 72) > 1 ? finiteNumber(car.ota?.speed, 72) : 72,
              }))
              return
            }
          } catch {
            // Keep the visual cockpit alive even when backend processes are stopped.
          }
          setApiTelemetry((prev) => ({ ...prev, connected: false }))
        }
      }
    }

    pollCockpitTelemetry()
    const timer = window.setInterval(pollCockpitTelemetry, 1000)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [vehicleId])

  return {
    battery: 82,
    connected: false,
    locationLabel: '판교 / 분당 방면',
    rangeKm: 312,
    roadSummary: '직진 주행',
    routeDirection: '판교 / 분당 방면',
    routeDistance: '120 m',
    speed,
    speedLimit: 80,
    ...apiTelemetry,
  }
}

function finiteNumber(value: unknown, fallback: number) {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? Math.round(parsed) : fallback
}

function clampPercent(value: unknown, fallback: number) {
  return Math.max(0, Math.min(100, finiteNumber(value, fallback)))
}

function HyundaiMark({ className = 'h-5 w-9' }: { className?: string }) {
  return (
    <svg viewBox="0 0 96 48" className={`${className} text-slate-100 drop-shadow-[0_0_12px_rgba(226,232,240,0.24)]`}>
      <ellipse cx="48" cy="24" rx="41" ry="17" fill="none" stroke="currentColor" strokeWidth="5" />
      <path
        d="M30 34 C36 18 40 13 45 13 M50 35 C57 18 61 13 66 13 M40 27 C49 21 55 20 63 19"
        fill="none"
        stroke="currentColor"
        strokeLinecap="round"
        strokeWidth="6"
      />
    </svg>
  )
}

function VehicleTopView({
  className = '',
  accent = '#22c55e',
  body = '#dbeafe',
}: {
  className?: string
  accent?: string
  body?: string
}) {
  const bodyGradientId = useId()
  const glowFilterId = useId()

  return (
    <svg viewBox="0 0 120 220" className={className}>
      <defs>
        <linearGradient id={bodyGradientId} x1="0" x2="1" y1="0" y2="1">
          <stop offset="0%" stopColor={body} />
          <stop offset="100%" stopColor="#64748b" />
        </linearGradient>
        <filter id={glowFilterId} x="-70%" y="-70%" width="240%" height="240%">
          <feGaussianBlur stdDeviation="5" result="blur" />
          <feColorMatrix
            in="blur"
            result="glow"
            type="matrix"
            values="0 0 0 0 0.12 0 0 0 0 0.85 0 0 0 0 0.76 0 0 0 0.55 0"
          />
          <feMerge>
            <feMergeNode in="glow" />
            <feMergeNode in="SourceGraphic" />
          </feMerge>
        </filter>
      </defs>
      <path
        d="M60 10 C78 11 89 35 93 71 L101 150 C104 184 86 207 60 210 C34 207 16 184 19 150 L27 71 C31 35 42 11 60 10Z"
        fill={`url(#${bodyGradientId})`}
        filter={`url(#${glowFilterId})`}
        stroke={accent}
        strokeWidth="3"
      />
      <path d="M38 54 C42 31 49 22 60 21 C71 22 78 31 82 54 L73 64 H47Z" fill="#111827" opacity="0.9" />
      <path d="M35 77 L47 68 H73 L85 77 L80 128 H40Z" fill="#dbeafe" opacity="0.75" />
      <path d="M35 143 H85 L82 181 C77 193 69 199 60 199 C51 199 43 193 38 181Z" fill="#cbd5e1" />
      <path d="M30 91 L16 103 M90 91 L104 103" stroke="#020617" strokeLinecap="round" strokeWidth="8" />
      <path d="M43 191 H77" stroke="#020617" strokeLinecap="round" strokeWidth="7" />
      <circle cx="37" cy="40" r="3" fill={accent} />
      <circle cx="83" cy="40" r="3" fill={accent} />
    </svg>
  )
}

function VehicleRear({ className = '' }: { className?: string }) {
  const gradientId = useId()

  return (
    <svg viewBox="0 0 260 150" className={className}>
      <defs>
        <linearGradient id={gradientId} x1="0" x2="1" y1="0" y2="1">
          <stop offset="0%" stopColor="#e8eef7" />
          <stop offset="52%" stopColor="#b8c5d6" />
          <stop offset="100%" stopColor="#64748b" />
        </linearGradient>
      </defs>
      <path
        d="M42 83 C51 47 79 28 130 28 C181 28 209 47 218 83 L232 111 C226 127 206 134 130 134 C54 134 34 127 28 111Z"
        fill={`url(#${gradientId})`}
        stroke="#dbeafe"
        strokeOpacity="0.65"
        strokeWidth="2"
      />
      <path d="M78 48 C90 34 109 29 130 29 C151 29 170 34 182 48 L174 76 H86Z" fill="#111827" opacity="0.88" />
      <path d="M48 91 H94" stroke="#ef4444" strokeLinecap="round" strokeWidth="7" />
      <path d="M166 91 H212" stroke="#ef4444" strokeLinecap="round" strokeWidth="7" />
      <path d="M105 112 H155" stroke="#020617" strokeLinecap="round" strokeWidth="7" />
      <circle cx="75" cy="129" r="14" fill="#020617" />
      <circle cx="185" cy="129" r="14" fill="#020617" />
    </svg>
  )
}

function VehicleSideProfile({ className = '' }: { className?: string }) {
  const sideGradientId = useId()

  return (
    <svg viewBox="0 0 260 120" className={className}>
      <defs>
        <linearGradient id={sideGradientId} x1="0" x2="1" y1="0" y2="1">
          <stop offset="0%" stopColor="#dbe7f4" />
          <stop offset="100%" stopColor="#64748b" />
        </linearGradient>
      </defs>
      <path
        d="M24 72 C43 43 76 30 121 30 H166 C196 31 224 48 240 73 L232 86 H28Z"
        fill={`url(#${sideGradientId})`}
        stroke="#94a3b8"
        strokeWidth="2"
      />
      <path d="M88 35 H151 C170 36 188 47 199 64 H72 C75 51 80 42 88 35Z" fill="#111827" opacity="0.82" />
      <circle cx="72" cy="86" r="17" fill="#050b14" stroke="#334155" strokeWidth="5" />
      <circle cx="199" cy="86" r="17" fill="#050b14" stroke="#334155" strokeWidth="5" />
      <path d="M33 70 H236" stroke="#f8fafc" strokeOpacity="0.38" strokeWidth="2" />
    </svg>
  )
}

function TopHeader({ connected }: { connected: boolean }) {
  const { vehicleId, viewMode, setVehicleId, setViewMode } = useCockpitStore()

  return (
    <header className="absolute left-0 right-0 top-0 z-50 flex h-10 items-center border-b border-white/8 bg-[#080c13]/95 text-slate-100 shadow-[0_10px_30px_rgba(0,0,0,0.34)]">
      <div className="flex h-full w-[50px] items-center justify-center border-r border-white/6">
        <HyundaiMark />
      </div>
      <div className="flex min-w-[320px] items-center gap-3 px-4">
        <span className="text-[13px] font-semibold tracking-wide">Hyundai SDV - Feature Operations Center</span>
        <select
          value={vehicleId}
          onChange={(event) => setVehicleId(event.target.value)}
          className="h-6 rounded-md border border-slate-500/20 bg-black/35 px-2 text-[10px] text-slate-200 outline-none"
        >
          {Array.from({ length: 10 }, (_, index) => (
            <option key={index + 1} value={`car_${index + 1}`}>
              car_{index + 1}
            </option>
          ))}
        </select>
        <div className={`flex items-center gap-1.5 text-[10px] ${connected ? 'text-emerald-300' : 'text-amber-300'}`}>
          <span
            className={`h-1.5 w-1.5 rounded-full ${
              connected ? 'bg-emerald-400 shadow-[0_0_12px_rgba(52,211,153,0.8)]' : 'bg-amber-300'
            }`}
          />
          {connected ? 'Live data' : 'Simulated'}
        </div>
      </div>

      <div className="mx-auto flex h-7 rounded-md border border-white/8 bg-black/25 p-0.5">
        {viewModes.map((mode) => (
          <button
            key={mode.id}
            onClick={() => setViewMode(mode.id)}
            className={`rounded px-4 text-[10px] transition ${
              viewMode === mode.id ? 'bg-sky-600 text-white' : 'text-slate-400 hover:text-slate-100'
            }`}
          >
            {mode.label}
          </button>
        ))}
      </div>

      <div className="flex min-w-[220px] items-center justify-end gap-3 px-4 text-[10px] text-slate-400">
        <span>2026-05-07 14:25:36</span>
        <FiBell className="text-sm text-slate-300" />
      </div>
    </header>
  )
}

function Sidebar() {
  return (
    <aside className="absolute bottom-0 left-0 top-10 z-40 w-[50px] border-r border-white/7 bg-[#080c13]/96 pt-3">
      <div className="flex flex-col items-center gap-2">
        {navItems.map((item, index) => {
          const Icon = item.icon
          const active = index === 0
          return (
            <motion.button
              key={item.label}
              whileHover={{ x: 1, scale: 1.04 }}
              className={`flex h-[42px] w-[42px] flex-col items-center justify-center rounded-lg text-[8px] transition ${
                active
                  ? 'bg-sky-500/18 text-sky-200 shadow-[0_0_18px_rgba(14,165,233,0.18)]'
                  : 'text-slate-500 hover:bg-white/5 hover:text-slate-200'
              }`}
            >
              <Icon className="mb-0.5 text-[14px]" />
              <span>{item.label}</span>
            </motion.button>
          )
        })}
      </div>
    </aside>
  )
}

function RoadsideTree({ size }: { size: number }) {
  return (
    <div className="relative" style={{ height: `${64 * size}px`, width: `${42 * size}px` }}>
      <div className="absolute bottom-[-2px] left-1/2 h-[7%] w-[78%] -translate-x-1/2 rounded-full bg-black/60 blur-[1px]" />
      <div className="absolute bottom-0 left-1/2 h-[48%] w-[14%] -translate-x-1/2 rounded-sm bg-[linear-gradient(180deg,#604127,#24160d)]" />
      <div className="absolute bottom-[28%] left-1/2 h-[58%] w-full -translate-x-1/2 rounded-[48%] bg-[radial-gradient(circle_at_42%_24%,#76aa74,#2e5c34_62%,#152c18)] shadow-[0_0_14px_rgba(22,101,52,0.28)]" />
      <div className="absolute bottom-[53%] left-1/2 h-[36%] w-[72%] -translate-x-1/2 rounded-full bg-[radial-gradient(circle_at_42%_28%,#8bbb7d,#355f36_68%,#172f19)]" />
    </div>
  )
}

function AnimatedRoadsideTreeMarker({ speed }: { speed: number }) {
  const duration = Math.max(2.4, 7 - speed * 0.05)

  return (
    <div className="pointer-events-none absolute inset-0 z-10 overflow-hidden">
      {[0, 0.5].map((phase) => (
        <motion.div
          key={phase}
          className="absolute left-[60%] top-[28.5%]"
          initial={{ opacity: 0 }}
          style={{ transformOrigin: '50% 100%' }}
          animate={{
            opacity: [0.92, 1, 0.96, 0],
            scale: [0.7, 0.92, 1.22, 1.58, 1.98, 2.48, 2.98, 3.48, 3.98],
            x: [0, 14, 31, 52, 73, 94, 115, 136, 157],
            y: [0, 44, 92, 138, 184, 230, 276, 322, 368, 414],
          }}
          transition={{
            delay: duration * phase,
            duration,
            ease: 'linear',
            repeat: Infinity,
          }}
        >
          <RoadsideTree size={0.78} />
        </motion.div>
      ))}
    </div>
  )
}

function RoadScene({ telemetry }: { telemetry: VehicleTelemetry }) {
  return (
    <div className="absolute inset-0 overflow-hidden bg-[#0b1119]">
      <div className="absolute inset-x-0 top-0 h-[46%] bg-[linear-gradient(180deg,rgba(73,93,112,0.72),rgba(32,46,60,0.55)_48%,rgba(8,12,18,0)_100%)]" />
      <div className="absolute left-[3%] right-[3%] top-[7%] h-[33%] opacity-65">
        {Array.from({ length: 30 }, (_, index) => (
          <div
            key={index}
            className="absolute bottom-0 rounded-t-sm bg-[linear-gradient(180deg,rgba(148,163,184,0.55),rgba(30,41,59,0.5))]"
            style={{
              left: `${index * 3.45}%`,
              width: `${1.7 + (index % 5) * 0.75}%`,
              height: `${34 + (index % 8) * 12}%`,
            }}
          />
        ))}
      </div>
      <div className="absolute inset-x-0 top-[23%] h-[16%] bg-[linear-gradient(180deg,rgba(5,8,12,0),rgba(5,8,12,0.55))]" />
      <div className="absolute inset-x-0 bottom-0 h-[58%] bg-[linear-gradient(180deg,rgba(8,12,18,0),rgba(2,6,12,0.76)_72%,rgba(1,3,8,0.96)_100%)]" />
      <div className="absolute bottom-[31%] left-1/2 h-[29%] w-[44%] -translate-x-1/2 [clip-path:polygon(42%_0,58%_0,74%_100%,26%_100%)] bg-[linear-gradient(180deg,#2b3642_0%,#1f2935_48%,#111923_100%)] shadow-[inset_0_1px_0_rgba(255,255,255,0.07),0_24px_48px_rgba(0,0,0,0.24)]" />
      <div className="absolute bottom-[31%] left-1/2 h-[29%] w-[44%] -translate-x-1/2 [clip-path:polygon(42%_0,58%_0,74%_100%,26%_100%)] bg-[radial-gradient(ellipse_at_50%_8%,rgba(203,213,225,0.13),transparent_36%),linear-gradient(90deg,rgba(0,0,0,0.22),transparent_26%,transparent_74%,rgba(0,0,0,0.22))]" />
      <AnimatedRoadsideTreeMarker speed={telemetry.speed} />
      <motion.div
        animate={{ y: [0, -3, 0] }}
        transition={{ duration: 3.6, repeat: Infinity, ease: 'easeInOut' }}
        className="absolute bottom-[32%] left-[51%] h-[72px] w-[126px] -translate-x-1/2"
      >
        <VehicleRear className="h-full w-full drop-shadow-[0_14px_18px_rgba(0,0,0,0.5)]" />
      </motion.div>
      <div className="absolute inset-0 bg-[radial-gradient(circle_at_48%_18%,rgba(186,230,253,0.08),transparent_28%),linear-gradient(180deg,rgba(2,6,23,0)_40%,rgba(2,6,23,0.62)_100%)]" />
    </div>
  )
}

function SteeringWheel() {
  return (
    <div className="absolute bottom-[-116px] left-[28px] h-[272px] w-[386px]">
      <div className="absolute inset-0 rounded-full border-35 border-[#03060c] bg-transparent shadow-[0_-24px_90px_rgba(0,0,0,0.86)]" />
      <div className="absolute left-1/2 top-[54%] h-[94px] w-[166px] -translate-x-1/2 -translate-y-1/2 rounded-[32px] border border-slate-600/45 bg-[linear-gradient(145deg,#1b2636,#05070d)] shadow-[inset_0_1px_0_rgba(255,255,255,0.08),0_22px_50px_rgba(0,0,0,0.45)]">
        <div className="absolute left-1/2 top-[18px] -translate-x-1/2">
          <HyundaiMark className="h-6 w-11" />
        </div>
        <div className="absolute bottom-[18px] left-1/2 -translate-x-1/2 text-[10px] font-semibold tracking-[0.26em] text-slate-500">
          HYUNDAI
        </div>
      </div>
      <div className="absolute left-[72px] top-[143px] h-5 w-[92px] rotate-[9deg] rounded-full bg-slate-950" />
      <div className="absolute right-[72px] top-[143px] h-5 w-[92px] rotate-[-9deg] rounded-full bg-slate-950" />
      <div className="absolute bottom-[34px] left-1/2 h-[90px] w-6 -translate-x-1/2 rounded-full bg-slate-950" />
    </div>
  )
}

function CockpitStage({ telemetry }: { telemetry: VehicleTelemetry }) {
  const vehicleId = useCockpitStore((state) => state.vehicleId)

  return (
    <main className="reference-panel absolute bottom-3 left-[58px] right-3 top-[48px] overflow-hidden rounded-sm">
      <RoadScene telemetry={telemetry} />
      <div className="absolute left-5 top-5 flex items-center gap-2 rounded bg-black/35 px-2 py-1 text-[10px] text-slate-300">
        <span>Vehicle</span>
        <span className="font-semibold text-slate-100">{vehicleId}</span>
        <span className="max-w-[220px] truncate text-slate-400">{telemetry.locationLabel}</span>
      </div>
      <div className="absolute left-6 top-[17%]">
        <div className="flex items-end gap-2">
          <motion.span
            key={telemetry.speed}
            initial={{ opacity: 0.45, y: -3 }}
            animate={{ opacity: 1, y: 0 }}
            className="text-[58px] font-semibold leading-none tracking-tight text-white"
          >
            {telemetry.speed}
          </motion.span>
          <span className="mb-1 text-[13px] text-slate-100">km/h</span>
        </div>
        <div className="mt-4 flex gap-3">
          <div className="flex h-10 w-10 items-center justify-center rounded-full border-3 border-red-400 bg-black/45 text-[18px] font-bold text-red-100 shadow-[0_0_16px_rgba(248,113,113,0.32)]">
            {telemetry.speedLimit}
          </div>
          <div className="flex h-10 w-10 items-center justify-center rounded-full border-3 border-emerald-400 bg-black/45 text-[18px] font-bold text-emerald-200 shadow-[0_0_16px_rgba(74,222,128,0.3)]">
            {telemetry.speedLimit}
          </div>
        </div>
      </div>
      <div className="absolute right-[26%] top-6 text-center text-[10px] font-semibold uppercase tracking-wider text-emerald-300 drop-shadow-[0_0_8px_rgba(52,211,153,0.45)]">
        <span className="text-[9px]">AUTO DRIVE</span>
        <br />
        ACTIVE
      </div>
      <div className="absolute right-4 top-[28%] rounded bg-black/30 px-2 py-1 text-center text-[11px] tracking-[0.35em] text-slate-400">
        P
        <br />R<br />N<br />
        <span className="text-slate-100">D</span>
      </div>
      <div className="absolute bottom-[31%] left-[47%] rounded bg-black/30 px-2 py-1 text-[11px] text-slate-100">
        <span className="mr-2 text-cyan-300">⚑</span>
        {telemetry.routeDistance}
        <div className="max-w-[180px] truncate text-[9px] text-slate-300">{telemetry.routeDirection}</div>
      </div>
      <DashboardCluster telemetry={telemetry} />
      <SteeringWheel />
    </main>
  )
}

function DashboardCluster({ telemetry }: { telemetry: VehicleTelemetry }) {
  return (
    <div className="absolute bottom-0 left-[25%] right-0 h-[30%] border-t border-white/8 bg-[linear-gradient(180deg,rgba(5,8,14,0.18),rgba(1,3,8,0.92))] pl-[190px] pr-3 pt-3">
      <div className="grid h-[86%] grid-cols-[0.9fr_1fr_1fr] gap-2">
        <ClusterCard
          title="km/h"
          value={`${telemetry.speed}`}
          sub={`${Math.round(telemetry.speed / 3.6)} m/s`}
          visual={<VehicleTopView className="h-14 w-8" />}
        />
        <ClusterCard
          title="NAVI"
          value={telemetry.routeDistance}
          sub={telemetry.routeDirection}
          visual={<MiniRoute />}
        />
        <ClusterCard
          title="EV"
          value={`${telemetry.battery}%`}
          sub={`Range ${telemetry.rangeKm} km`}
          visual={<VehicleSideProfile className="h-14 w-24 opacity-80" />}
        />
      </div>
    </div>
  )
}

function MiniRoute() {
  return (
    <div className="relative h-14 w-24 overflow-hidden rounded bg-[#0a1320]">
      <div className="absolute inset-0 bg-[radial-gradient(circle_at_50%_50%,rgba(14,165,233,0.18),transparent_55%)]" />
      <div className="absolute left-1/2 top-0 h-full w-1 -translate-x-1/2 bg-cyan-300/55 shadow-[0_0_12px_rgba(34,211,238,0.7)]" />
      <div className="absolute bottom-2 left-1/2 h-2 w-2 -translate-x-1/2 rounded-full bg-white" />
      <div className="absolute left-6 top-4 h-8 w-px rotate-45 bg-slate-500/60" />
      <div className="absolute right-6 top-3 h-9 w-px rotate-[-36deg] bg-slate-500/50" />
    </div>
  )
}

function ClusterCard({ title, value, sub, visual }: { title: string; value: string; sub: string; visual?: ReactNode }) {
  return (
    <div className="relative overflow-hidden rounded border border-white/8 bg-black/36 p-3">
      <div className="relative z-10">
        <div className="mb-1 text-[9px] font-semibold uppercase tracking-widest text-slate-500">{title}</div>
        <div className="text-[28px] font-semibold leading-none text-white">{value}</div>
        <div className="mt-1 text-[10px] text-slate-300">{sub}</div>
      </div>
      {visual ? <div className="absolute bottom-2 right-3 opacity-90">{visual}</div> : null}
    </div>
  )
}

function MiniMapPanel() {
  return (
    <section className="reference-panel absolute right-[268px] top-[48px] h-[176px] w-[298px] overflow-hidden rounded-sm p-2.5">
      <div className="relative h-full overflow-hidden rounded-sm bg-[#08101b]">
        <div className="absolute inset-0 bg-[linear-gradient(90deg,rgba(255,255,255,0.05)_1px,transparent_1px),linear-gradient(rgba(255,255,255,0.04)_1px,transparent_1px)] bg-size-[34px_34px]" />
        <div className="absolute left-[42%] top-0 h-full w-[22%] rotate-12 bg-slate-700/28" />
        <div className="absolute left-[50%] top-0 h-full w-1 -translate-x-1/2 bg-emerald-300 shadow-[0_0_16px_rgba(52,211,153,0.85)]" />
        <div className="absolute left-[57%] top-2 h-full w-px bg-white/20" />
        <VehicleTopView className="absolute bottom-4 left-1/2 h-16 w-9 -translate-x-1/2" />
        <VehicleTopView className="absolute left-[33%] top-9 h-12 w-7 rotate-[-8deg] opacity-55" accent="#94a3b8" body="#94a3b8" />
        <div className="absolute right-2 top-2 rounded bg-black/35 px-2 py-1 text-[9px] font-semibold text-emerald-300">TRACKING</div>
      </div>
    </section>
  )
}

function SystemStatusPanel() {
  return (
    <section className="reference-panel absolute right-[268px] top-[232px] h-[158px] w-[298px] rounded-sm p-3">
      <div className="mb-3 text-[11px] font-semibold tracking-wide text-slate-300">시스템 상태</div>
      <div className="grid h-[112px] grid-cols-3 gap-2">
        <StatusTile title="ADAS" version="v3.0.1" state="RUNNING" cpu="12%" mem="715 MB" />
        <StatusTile title="Navigation" version="v2.1.0" state="RUNNING" cpu="8%" mem="96 MB" />
        <StatusTile title="EV Energy" version="v2.0.0" state="RUNNING" cpu="15%" mem="125 MB" />
      </div>
    </section>
  )
}

function StatusTile({
  title,
  version,
  state,
  cpu,
  mem,
}: {
  title: string
  version: string
  state: string
  cpu: string
  mem: string
}) {
  return (
    <div className="rounded border border-white/7 bg-black/24 p-2">
      <div className="mb-1 text-[10px] font-semibold text-slate-200">{title}</div>
      <div className="text-[9px] text-slate-500">{version}</div>
      <div className="mt-2 text-[9px] font-bold text-emerald-300">{state}</div>
      <div className="mt-2 grid grid-cols-2 gap-1 text-[8px] text-slate-500">
        <span>CPU</span>
        <span>{cpu}</span>
        <span>MEM</span>
        <span>{mem}</span>
      </div>
    </div>
  )
}

function OperationsCenter() {
  return (
    <aside className="absolute bottom-2 right-2 top-[48px] w-[250px] space-y-2">
      <OpsPanel title="OTA 배포 제어">
        <MetricRow label="활성 버전" value="v3.0.1" />
        <MetricRow label="타겟 버전" value="v3.1.0" />
        <ProgressBar value={100} color="emerald" />
        <CheckList labels={['Download', 'Verify', 'Install', 'Switch', 'Validate', 'Complete']} />
        <div className="mt-2 grid grid-cols-2 gap-2">
          <button className="rounded bg-sky-600 px-2 py-1 text-[10px] font-semibold text-white">배포 시작</button>
          <button className="rounded bg-slate-800 px-2 py-1 text-[10px] text-slate-300">롤백</button>
        </div>
      </OpsPanel>
      <OpsPanel title="A/B 테스트">
        <MetricRow label="관제" value="Traffic Split" />
        <MetricRow label="A v3.0.1" value="80%" />
        <ProgressBar value={80} color="sky" compact />
        <MetricRow label="B v3.1.0" value="20%" />
        <ProgressBar value={20} color="sky" compact />
      </OpsPanel>
      <OpsPanel title="Shadow 검증">
        <MetricRow label="상태" value="VALIDATION PASS" good />
        <MetricRow label="Lane Diff" value="1.2%" />
        <MetricRow label="Braking Delay" value="20 ms" />
        <MetricRow label="Route Deviation" value="0.8 m" />
      </OpsPanel>
      <OpsPanel title="Health 모니터링">
        <Meter label="CPU" value="23%" width="23%" />
        <Meter label="Memory" value="2.1GB / 8GB" width="36%" />
        <MetricRow label="Event Throughput" value="152 msg/s" />
        <MetricRow label="Heartbeat" value="OK" good />
      </OpsPanel>
    </aside>
  )
}

function OpsPanel({ title, children }: { title: string; children: ReactNode }) {
  return (
    <motion.section whileHover={{ y: -1 }} className="reference-panel rounded-sm p-3">
      <h2 className="mb-2 text-[10px] font-bold tracking-wide text-slate-400">{title}</h2>
      {children}
    </motion.section>
  )
}

function MetricRow({ label, value, good = false }: { label: string; value: string; good?: boolean }) {
  return (
    <div className="mb-1.5 flex items-center justify-between text-[10px]">
      <span className="text-slate-500">{label}</span>
      <span className={good ? 'font-semibold text-emerald-300' : 'text-slate-200'}>{value}</span>
    </div>
  )
}

function ProgressBar({ value, color, compact = false }: { value: number; color: 'emerald' | 'sky'; compact?: boolean }) {
  const barColor = color === 'emerald' ? 'bg-emerald-400' : 'bg-sky-500'

  return (
    <div className={`${compact ? 'mb-1.5 h-1' : 'mt-2 h-1.5'} overflow-hidden rounded-full bg-slate-800`}>
      <div className={`${barColor} h-full rounded-full shadow-[0_0_14px_rgba(56,189,248,0.35)]`} style={{ width: `${value}%` }} />
    </div>
  )
}

function CheckList({ labels }: { labels: string[] }) {
  return (
    <div className="mt-2 grid grid-cols-2 gap-x-2 gap-y-1 text-[9px] text-slate-400">
      {labels.map((label) => (
        <div key={label} className="flex items-center gap-1">
          <span className="h-1.5 w-1.5 rounded-full bg-emerald-400" />
          {label}
        </div>
      ))}
    </div>
  )
}

function Meter({ label, value, width }: { label: string; value: string; width: string }) {
  return (
    <div className="mb-2 grid grid-cols-[54px_1fr_62px] items-center gap-2 text-[10px]">
      <span className="text-slate-500">{label}</span>
      <div className="h-1 overflow-hidden rounded-full bg-slate-800">
        <div className="h-full rounded-full bg-sky-500" style={{ width }} />
      </div>
      <span className="text-right text-slate-300">{value}</span>
    </div>
  )
}

function EventTimeline() {
  return (
    <section className="reference-panel absolute bottom-2 left-[58px] right-[268px] h-[112px] rounded-sm p-3">
      <div className="mb-2 grid grid-cols-[92px_1fr_150px_92px] text-[9px] font-bold uppercase tracking-widest text-slate-500">
        <span>시간</span>
        <span>이벤트</span>
        <span>대상</span>
        <span>상태</span>
      </div>
      <div className="space-y-1">
        {events.map((event) => (
          <div
            key={event.join('-')}
            className="grid grid-cols-[92px_1fr_150px_92px] rounded bg-white/2.5 px-2 py-0.5 text-[10px]"
          >
            <span className="text-slate-500">{event[0]}</span>
            <span className="text-slate-200">{event[1]}</span>
            <span className="text-slate-400">{event[2]}</span>
            <span className="font-semibold text-emerald-300">{event[3]}</span>
          </div>
        ))}
      </div>
    </section>
  )
}

function App() {
  const vehicleId = useCockpitStore((state) => state.vehicleId)
  const telemetry = useVehicleTelemetry(vehicleId)

  return (
    <div className="relative h-screen min-h-0 w-screen min-w-0 overflow-hidden bg-[#050910] text-slate-100">
      <div className="absolute inset-0 bg-[radial-gradient(circle_at_42%_20%,rgba(56,189,248,0.12),transparent_32%),radial-gradient(circle_at_85%_18%,rgba(34,197,94,0.08),transparent_26%)]" />
      <TopHeader connected={telemetry.connected} />
      <Sidebar />
      <CockpitStage telemetry={telemetry} />
    </div>
  )
}

export default App
