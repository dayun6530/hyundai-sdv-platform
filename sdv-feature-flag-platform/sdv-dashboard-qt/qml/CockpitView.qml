import QtQuick 6.5

Item {
    id: cockpit
    width: 1280
    height: 520
    clip: true

    property real t: 0
    property string vehicleId: "car_1"
    property bool mqttConnected: false
    property real speedKph: 72
    property int batteryPct: 82
    property int rangeKm: 312
    property string adasVersion: "v3.0"
    property string navVersion: "v2.1"
    property string evVersion: "v2.0"
    property bool autonomous: true
    property bool navActive: true
    property bool evActive: true
    property bool shadowMode: true
    property bool upgraded: true
    property int otaProgress: 100
    property string otaPhase: "COMPLETED"
    property string shadowStatus: "VALIDATION PASS"
    property string selectedView: "cockpit"
    property string selectedMenu: "home"
    signal carSelected(string carId)

    Timer {
        interval: 16
        running: true
        repeat: true
        onTriggered: cockpit.t += 0.016
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#07111f" }
            GradientStop { position: 0.43; color: "#101b2d" }
            GradientStop { position: 1.0; color: "#03060b" }
        }
    }

    Rectangle {
        x: 58
        y: 42
        width: parent.width - 58
        height: parent.height - 42
        color: "transparent"
        border.width: 1
        border.color: "#16243a"
        opacity: 0.9
    }

    Canvas {
        id: world
        anchors.fill: parent
        property real time: cockpit.t
        onTimeChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            ctx.reset()

            const sky = ctx.createLinearGradient(0, 0, 0, h * 0.48)
            sky.addColorStop(0, "#0d1726")
            sky.addColorStop(0.58, "#1f2d40")
            sky.addColorStop(1, "#334356")
            ctx.fillStyle = sky
            ctx.fillRect(0, 0, w, h * 0.48)

            // City/highway backdrop, deliberately photographic in composition but procedural for portability.
            for (let i = 0; i < 42; ++i) {
                const bw = 24 + (i % 5) * 12
                const bh = 72 + (i % 7) * 25
                const x = i * w / 41 - bw / 2
                const y = h * (0.30 - (i % 4) * 0.012)
                ctx.fillStyle = i % 3 === 0 ? "rgba(45,63,88,0.82)" : "rgba(27,39,58,0.76)"
                ctx.fillRect(x, y, bw, bh)
                ctx.fillStyle = "rgba(191,219,254,0.16)"
                for (let j = 0; j < 8; ++j) {
                    ctx.fillRect(x + 7 + (j % 3) * 10, y + 10 + Math.floor(j / 3) * 18, 3, 8)
                }
            }

            // Tree/median layers add windshield depth without using any bitmap.
            for (let i = 0; i < 34; ++i) {
                const x = i * w / 33
                const side = i < 17 ? -1 : 1
                const trunkX = side < 0 ? x * 0.68 : w - (w - x) * 0.68
                const y = h * (0.38 + (i % 5) * 0.004)
                ctx.fillStyle = "rgba(26,43,39,0.78)"
                ctx.beginPath()
                ctx.arc(trunkX, y, 18 + (i % 4) * 3, 0, Math.PI * 2)
                ctx.fill()
                ctx.fillStyle = "rgba(70,84,74,0.60)"
                ctx.fillRect(trunkX - 2, y + 8, 4, 42)
            }

            const glow = ctx.createRadialGradient(w * 0.54, h * 0.29, 40, w * 0.54, h * 0.29, w * 0.55)
            glow.addColorStop(0, cockpit.upgraded ? "rgba(94,234,212,0.18)" : "rgba(96,165,250,0.12)")
            glow.addColorStop(1, "rgba(0,0,0,0)")
            ctx.fillStyle = glow
            ctx.fillRect(0, 0, w, h)

            // Road with real first-person perspective.
            const lNear = { x: w * 0.035, y: h * 0.82 }
            const rNear = { x: w * 0.965, y: h * 0.82 }
            const lFar = { x: w * 0.450, y: h * 0.360 }
            const rFar = { x: w * 0.585, y: h * 0.360 }

            const roadGrad = ctx.createLinearGradient(w * 0.5, h * 0.34, w * 0.5, h * 0.78)
            roadGrad.addColorStop(0, "#18212c")
            roadGrad.addColorStop(0.64, "#253140")
            roadGrad.addColorStop(1, "#111820")
            ctx.fillStyle = roadGrad
            ctx.beginPath()
            ctx.moveTo(lNear.x, lNear.y)
            ctx.lineTo(lFar.x, lFar.y)
            ctx.lineTo(rFar.x, rFar.y)
            ctx.lineTo(rNear.x, rNear.y)
            ctx.closePath()
            ctx.fill()

            // Highway shoulders / guard rails.
            ctx.strokeStyle = "rgba(117,139,162,0.62)"
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.moveTo(w * 0.09, h * 0.72)
            ctx.lineTo(w * 0.44, h * 0.39)
            ctx.moveTo(w * 0.91, h * 0.72)
            ctx.lineTo(w * 0.60, h * 0.39)
            ctx.stroke()
            ctx.strokeStyle = "rgba(59,79,99,0.55)"
            ctx.lineWidth = 8
            ctx.beginPath()
            ctx.moveTo(w * 0.00, h * 0.58)
            ctx.lineTo(w * 0.31, h * 0.72)
            ctx.moveTo(w * 1.00, h * 0.58)
            ctx.lineTo(w * 0.69, h * 0.72)
            ctx.stroke()

            ctx.strokeStyle = "rgba(150,160,176,0.45)"
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.moveTo(lNear.x, lNear.y)
            ctx.lineTo(lFar.x, lFar.y)
            ctx.moveTo(rNear.x, rNear.y)
            ctx.lineTo(rFar.x, rFar.y)
            ctx.stroke()

            for (let i = 1; i < 7; ++i) {
                const p = i / 7
                ctx.strokeStyle = "rgba(168,196,217,0.20)"
                ctx.lineWidth = 1.4
                ctx.beginPath()
                ctx.moveTo(lerp(lNear.x, lFar.x, p), lerp(lNear.y, lFar.y, p))
                ctx.lineTo(lerp(rNear.x, rFar.x, p), lerp(rNear.y, rFar.y, p))
                ctx.stroke()
            }

            // Road motion dashes moving toward the driver.
            for (let i = 0; i < 12; ++i) {
                const p = (i * 0.095 + time * 0.27) % 1.0
                const left = pointLerp(lNear, lFar, p)
                const right = pointLerp(rNear, rFar, p)
                const mid = pointLerp(left, right, 0.5)
                const ww = 10 + (1 - p) * 56
                ctx.fillStyle = `rgba(226,232,240,${0.18 + (1 - p) * 0.45})`
                roundRect(ctx, mid.x - ww / 2, mid.y - 2, ww, 4, 2)
                ctx.fill()
            }
        }
    }

    Canvas {
        id: adas
        anchors.fill: parent
        property real time: cockpit.t
        onTimeChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            ctx.reset()

            const sway = Math.sin(time * 1.1) * 3
            const lNear = { x: w * 0.36 + sway, y: h * 0.76 }
            const rNear = { x: w * 0.64 - sway, y: h * 0.76 }
            const lFar = { x: w * 0.492, y: h * 0.37 }
            const rFar = { x: w * 0.545, y: h * 0.37 }

            drawLane(ctx, lNear, lFar, cockpit.upgraded ? "#34f5df" : "#2fa7ff", cockpit.upgraded ? 8 : 5)
            drawLane(ctx, rNear, rFar, cockpit.upgraded ? "#34f5df" : "#2fa7ff", cockpit.upgraded ? 8 : 5)

            if (cockpit.shadowMode) {
                ctx.setLineDash([8, 7])
                ctx.strokeStyle = "rgba(226,232,240,0.58)"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(lNear.x + 24, lNear.y - 4)
                ctx.lineTo(lFar.x + 5, lFar.y)
                ctx.moveTo(rNear.x - 24, rNear.y + 4)
                ctx.lineTo(rFar.x - 5, rFar.y)
                ctx.stroke()
                ctx.setLineDash([])
            }

            // Glowing route projected onto the road.
            const cNear = pointLerp(lNear, rNear, 0.5)
            const cFar = pointLerp(lFar, rFar, 0.5)
            ctx.strokeStyle = cockpit.upgraded ? "rgba(94,234,212,0.34)" : "rgba(96,165,250,0.28)"
            ctx.lineWidth = cockpit.upgraded ? 34 : 24
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(cNear.x, cNear.y - 6)
            ctx.bezierCurveTo(w * 0.47, h * 0.60, w * 0.56, h * 0.49, cFar.x, cFar.y + 8)
            ctx.stroke()
            ctx.strokeStyle = cockpit.upgraded ? "#5eead4" : "#60a5fa"
            ctx.lineWidth = cockpit.upgraded ? 7 : 5
            ctx.beginPath()
            ctx.moveTo(cNear.x, cNear.y - 6)
            ctx.bezierCurveTo(w * 0.47, h * 0.60, w * 0.56, h * 0.49, cFar.x, cFar.y + 8)
            ctx.stroke()
        }
    }

    DetectedVehicle { x: parent.width * 0.47; y: parent.height * 0.43; scale: 0.70; locked: true }
    DetectedVehicle { x: parent.width * 0.34; y: parent.height * 0.51; scale: 0.48; locked: false }
    DetectedVehicle { x: parent.width * 0.66; y: parent.height * 0.53; scale: 0.45; locked: false }

    SurroundCameraPanel {
        x: parent.width - width - 28
        y: 62
        width: 318
        height: 168
        time: cockpit.t
        autonomous: cockpit.autonomous
        navActive: cockpit.navActive
    }

    OperationsOverlay {
        x: 74
        y: 62
        width: 236
        height: 136
        autonomous: cockpit.autonomous
        navActive: cockpit.navActive
        evActive: cockpit.evActive
        upgraded: cockpit.upgraded
        mqttConnected: cockpit.mqttConnected
        otaProgress: cockpit.otaProgress
        otaPhase: cockpit.otaPhase
        shadowStatus: cockpit.shadowStatus
    }

    SystemStatusCluster {
        x: parent.width - width - 28
        y: 240
        width: 318
        height: 116
        autonomous: cockpit.autonomous
        navActive: cockpit.navActive
        evActive: cockpit.evActive
        adasVersion: cockpit.adasVersion
        navVersion: cockpit.navVersion
        evVersion: cockpit.evVersion
    }

    ViewContextPanel {
        x: 326
        y: 62
        width: 204
        height: 54
        selectedView: cockpit.selectedView
        selectedMenu: cockpit.selectedMenu
    }

    WindshieldHud {
        anchors.fill: parent
        time: cockpit.t
        autonomous: cockpit.autonomous
        navActive: cockpit.navActive
        upgraded: cockpit.upgraded
        otaProgress: cockpit.otaProgress
        shadowStatus: cockpit.shadowStatus
    }

    RoadConfidence {
        x: parent.width * 0.5 - width / 2
        y: parent.height * 0.405
        width: 220
        height: 72
        autonomous: cockpit.autonomous
        upgraded: cockpit.upgraded
        shadowMode: cockpit.shadowMode
    }

    WindshieldFrame {
        anchors.fill: parent
        time: cockpit.t
    }

    CockpitInterior {
        anchors.fill: parent
        time: cockpit.t
    }

    InstrumentCluster {
        x: 58
        y: parent.height - height - 38
        width: 306
        height: 124
        time: cockpit.t
        speedKph: cockpit.speedKph
        autonomous: cockpit.autonomous
        adasVersion: cockpit.adasVersion
    }

    NavigationCard {
        x: 390
        y: parent.height - height - 42
        width: 278
        height: 112
        time: cockpit.t
        navActive: cockpit.navActive
        navVersion: cockpit.navVersion
    }

    EvCard {
        x: parent.width - width - 34
        y: parent.height - height - 42
        width: 284
        height: 112
        time: cockpit.t
        batteryPct: cockpit.batteryPct
        rangeKm: cockpit.rangeKm
        evActive: cockpit.evActive
        evVersion: cockpit.evVersion
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 18
        radius: 24
        color: "transparent"
        border.width: 6
        border.color: Qt.rgba(1.0, 0.22, 0.22, 0.18 + Math.max(0, Math.sin(cockpit.t * 2.7)) * 0.16)
        visible: Math.sin(cockpit.t * 2.7) > 0.32
    }

    TopBar {
        id: topBar
        x: 0
        y: 0
        width: parent.width
        height: 42
        vehicleId: cockpit.vehicleId
        mqttConnected: cockpit.mqttConnected
        selectedView: cockpit.selectedView
        onViewSelected: function(view) { cockpit.selectedView = view }
        onCarPicked: function(carId) { cockpit.carSelected(carId) }
    }

    SideMenu {
        id: sideMenu
        x: 0
        y: 42
        width: 58
        height: parent.height - 42
        selectedMenu: cockpit.selectedMenu
        onMenuSelected: function(menu) { cockpit.selectedMenu = menu }
    }

    Rectangle {
        id: cockpitFrame
        x: 58
        y: 42
        width: parent.width - 58
        height: parent.height - 42
        color: "transparent"
        border.width: 1
        border.color: "#273a52"
        opacity: 0.78
    }

    component DetectedVehicle: Item {
        property bool locked: false
        width: 76
        height: 86
        Rectangle {
            anchors.centerIn: parent
            width: 58
            height: 72
            radius: 10
            color: "#dbe5ef"
            border.width: 2
            border.color: locked ? "#5eead4" : "#94a3b8"
        }
        Rectangle {
            x: 21
            y: 16
            width: 34
            height: 18
            radius: 4
            color: "#172033"
            opacity: 0.8
        }
        Rectangle {
            anchors.centerIn: parent
            width: 76
            height: 86
            radius: 14
            color: "transparent"
            border.width: locked ? 2 : 1
            border.color: locked ? "#5eead4" : "#94a3b8"
            opacity: locked ? 0.9 : 0.45
        }
    }

    component MiniMap: Rectangle {
        property real time: 0
        property bool shadowMode: false
        property bool navActive: true
        radius: 16
        color: "#08101ccd"
        border.color: "#53657d88"
        border.width: 1
        clip: true
        Text { x: 14; y: 10; text: shadowMode ? "MINIMAP / SHADOW" : "MINIMAP"; color: "#dbeafe"; font.pixelSize: 11; font.bold: true }
        Text { anchors.right: parent.right; anchors.rightMargin: 14; y: 10; text: navActive ? "NAV LINK" : "NAV OFF"; color: navActive ? "#5eead4" : "#f97316"; font.pixelSize: 10; font.bold: true }
        Canvas {
            anchors.fill: parent
            property real localTime: parent.time
            property bool localNavActive: parent.navActive
            onLocalTimeChanged: requestPaint()
            onLocalNavActiveChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                const w = width
                const h = height
                ctx.reset()
                ctx.strokeStyle = "#52667f"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(w * 0.50, h * 0.92)
                ctx.lineTo(w * 0.50, h * 0.08)
                ctx.stroke()
                ctx.strokeStyle = localNavActive ? "#37d7ff" : "#64748b"
                ctx.lineWidth = localNavActive ? 4 : 2
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(w * 0.50, h * 0.88)
                ctx.bezierCurveTo(w * 0.38, h * 0.60, w * 0.72, h * 0.48, w * 0.82, h * 0.18)
                ctx.stroke()
            }
        }
        Rectangle { x: width * 0.47; y: height * 0.73; width: 18; height: 30; radius: 5; color: "#f8fafc" }
        Rectangle { x: width * 0.30; y: height * 0.54; width: 10; height: 16; radius: 3; color: navActive ? "#5eead4" : "#64748b"; opacity: 0.9 }
        Rectangle { x: width * 0.62; y: height * 0.36; width: 10; height: 16; radius: 3; color: "#94a3b8"; opacity: 0.8 }
    }

    component SurroundCameraPanel: Rectangle {
        property real time: 0
        property bool autonomous: true
        property bool navActive: true
        radius: 12
        color: "#050b14dd"
        border.color: "#344760aa"
        border.width: 1
        clip: true

        Text {
            x: 12
            y: 9
            text: "SURROUND / LANE MODEL"
            color: "#8ea6c7"
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 0.5
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 9
            text: autonomous ? "AUTO DRIVE" : "MANUAL"
            color: autonomous ? "#5eead4" : "#fbbf24"
            font.pixelSize: 9
            font.bold: true
        }

        Canvas {
            anchors.fill: parent
            anchors.topMargin: 26
            property real localTime: parent.time
            property bool localAuto: parent.autonomous
            property bool localNav: parent.navActive
            onLocalTimeChanged: requestPaint()
            onLocalAutoChanged: requestPaint()
            onLocalNavChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                const w = width
                const h = height
                ctx.reset()

                const bg = ctx.createLinearGradient(0, 0, 0, h)
                bg.addColorStop(0, "#121b27")
                bg.addColorStop(1, "#050b14")
                ctx.fillStyle = bg
                ctx.fillRect(0, 0, w, h)

                const laneColor = localAuto ? "#2dd4bf" : "#64748b"
                ctx.strokeStyle = "rgba(148,163,184,0.22)"
                ctx.lineWidth = 1
                for (let i = 0; i < 7; ++i) {
                    const x = w * (0.20 + i * 0.10)
                    ctx.beginPath()
                    ctx.moveTo(x, 0)
                    ctx.lineTo(x - w * 0.22, h)
                    ctx.stroke()
                }

                ctx.strokeStyle = "#52667f"
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(w * 0.39, 0)
                ctx.lineTo(w * 0.24, h)
                ctx.moveTo(w * 0.61, 0)
                ctx.lineTo(w * 0.76, h)
                ctx.stroke()

                ctx.strokeStyle = laneColor
                ctx.lineWidth = 3
                ctx.shadowColor = laneColor
                ctx.shadowBlur = localAuto ? 12 : 0
                ctx.beginPath()
                ctx.moveTo(w * 0.50, 0)
                ctx.bezierCurveTo(w * 0.48, h * 0.32, w * 0.53, h * 0.60, w * 0.50, h)
                ctx.stroke()
                ctx.shadowBlur = 0

                function car(x, y, s, color, locked) {
                    ctx.fillStyle = color
                    roundRect(ctx, x - 10 * s, y - 18 * s, 20 * s, 36 * s, 5 * s)
                    ctx.fill()
                    ctx.strokeStyle = locked ? "#5eead4" : "#94a3b8"
                    ctx.lineWidth = locked ? 2 : 1
                    ctx.stroke()
                    ctx.fillStyle = "#101827"
                    roundRect(ctx, x - 6 * s, y - 10 * s, 12 * s, 8 * s, 2)
                    ctx.fill()
                }

                car(w * 0.50, h * 0.70 + Math.sin(localTime * 1.5) * 2, 1.05, "#e2e8f0", true)
                car(w * 0.43, h * 0.34, 0.65, "#94a3b8", false)
                car(w * 0.62, h * 0.42, 0.60, "#64748b", false)

                ctx.fillStyle = localNav ? "rgba(45,212,191,0.16)" : "rgba(100,116,139,0.12)"
                roundRect(ctx, w * 0.47, h * 0.10, w * 0.06, h * 0.82, 12)
                ctx.fill()
            }
        }
    }

    component WindshieldHud: Item {
        property real time: 0
        property bool autonomous: true
        property bool navActive: true
        property bool upgraded: true
        property int otaProgress: 100
        property string shadowStatus: "VALIDATION PASS"
        Row {
            x: parent.width * 0.43
            y: 54
            spacing: 8
            HudPill { text: autonomous ? "AUTO DRIVE ACTIVE" : "ADAS STANDBY"; active: autonomous; width: 164 }
            HudPill { text: upgraded ? "OTA vNEXT" : "OTA " + otaProgress + "%"; active: upgraded || otaProgress > 0; width: 106 }
        }
        Text {
            x: parent.width * 0.425
            y: 120
            width: 240
            horizontalAlignment: Text.AlignHCenter
            text: "↱ 120 m\n판교 / 분당 방면"
            color: "#d8f3ff"
            font.pixelSize: 15
            font.bold: true
            opacity: 0.92
        }
        Row {
            x: parent.width * 0.34
            y: 86
            spacing: 10
            HudPill { text: "LKA"; active: autonomous; width: 70 }
            HudPill { text: "ACC"; active: autonomous; width: 70 }
            HudPill { text: "NAV"; active: navActive; width: 70 }
            HudPill { text: shadowStatus.indexOf("FAIL") >= 0 ? "SHADOW NG" : "SHADOW OK"; active: shadowStatus.indexOf("FAIL") < 0; width: 104 }
        }
    }

    component RoadConfidence: Rectangle {
        property bool autonomous: true
        property bool upgraded: true
        property bool shadowMode: false
        radius: 18
        color: "#06101a99"
        border.color: autonomous ? "#5eead466" : "#f9731666"
        border.width: 1
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            text: autonomous ? "LANE MODEL CONFIDENCE" : "DRIVER CONTROL"
            color: autonomous ? "#99f6e4" : "#fed7aa"
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 0.5
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 34
            spacing: 6
            Repeater {
                model: 6
                Rectangle {
                    width: 24
                    height: 8
                    radius: 4
                    color: autonomous ? (index < 5 ? "#2dd4bf" : "#134e4a") : "#4b5563"
                    opacity: 0.76 + Math.sin(cockpit.t * 3 + index) * 0.14
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 52
            text: shadowMode ? "shadow validation active" : (upgraded ? "OTA profile applied" : "base profile")
            color: "#8fa4bf"
            font.pixelSize: 9
        }
    }

    component HudPill: Rectangle {
        property string text: ""
        property bool active: false
        height: 30
        radius: 15
        color: active ? "#0b322dcc" : "#101827bb"
        border.color: active ? "#5eead4" : "#53657d"
        border.width: 1
        Text { anchors.centerIn: parent; text: parent.text; color: parent.active ? "#86f7dc" : "#8aa0ba"; font.pixelSize: 11; font.bold: true }
    }

    component OperationsOverlay: Rectangle {
        property bool autonomous: true
        property bool navActive: true
        property bool evActive: true
        property bool upgraded: true
        property bool mqttConnected: false
        property int otaProgress: 0
        property string otaPhase: ""
        property string shadowStatus: "VALIDATION PASS"
        radius: 18
        color: "#07101bd8"
        border.color: "#3e526dcc"
        border.width: 1
        clip: true

        Rectangle {
            anchors.fill: parent
            opacity: 0.38
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#193153" }
                GradientStop { position: 1.0; color: "#06101b" }
            }
        }

        Text { x: 14; y: 13; width: 136; elide: Text.ElideRight; text: "SDV ORCHESTRATION"; color: "#dbeafe"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.4 }
        Text { anchors.right: parent.right; anchors.rightMargin: 14; y: 13; text: mqttConnected ? "MQTT" : "HTTP"; color: mqttConnected ? "#5eead4" : "#fbbf24"; font.pixelSize: 10; font.bold: true }

        Rectangle { x: 14; y: 38; width: parent.width - 28; height: 1; color: "#31445f" }

        StatusDot { x: 14; y: 52; label: "ADAS"; active: autonomous; detail: autonomous ? "active" : "standby" }
        StatusDot { x: 88; y: 52; label: "NAV"; active: navActive; detail: navActive ? "route" : "off" }
        StatusDot { x: 158; y: 52; label: "EV"; active: evActive; detail: evActive ? "energy" : "fallback" }

        Text { x: 14; y: 92; width: parent.width - 28; elide: Text.ElideRight; text: upgraded ? "OTA enhanced cockpit profile" : "OTA: " + (otaPhase.length > 0 ? otaPhase : "idle"); color: upgraded ? "#99f6e4" : "#bfdbfe"; font.pixelSize: 10; font.bold: true }
        Rectangle { x: 14; y: 114; width: parent.width - 28; height: 7; radius: 4; color: "#101a29" }
        Rectangle { x: 14; y: 114; width: (parent.width - 28) * Math.max(0, Math.min(100, otaProgress)) / 100; height: 7; radius: 4; color: upgraded ? "#2dd4bf" : "#38bdf8" }
        Text { anchors.right: parent.right; anchors.rightMargin: 14; y: 123; text: shadowStatus.indexOf("FAIL") >= 0 ? "SHADOW FAIL" : "SHADOW OK"; color: shadowStatus.indexOf("FAIL") >= 0 ? "#fb7185" : "#86efac"; font.pixelSize: 9; font.bold: true }
    }

    component StatusDot: Item {
        property string label: ""
        property string detail: ""
        property bool active: false
        width: 62
        height: 32
        Rectangle {
            x: 0
            y: 4
            width: 9
            height: 9
            radius: 5
            color: active ? "#34d399" : "#f97316"
            opacity: active ? 1.0 : 0.82
        }
        Text { x: 15; y: 0; text: label; color: "#f8fafc"; font.pixelSize: 11; font.bold: true }
        Text { x: 15; y: 16; text: detail; color: "#8fa4bf"; font.pixelSize: 9 }
    }

    component FeatureRail: Rectangle {
        property bool autonomous: true
        property bool navActive: true
        property bool evActive: true
        radius: 18
        color: "#07101bd8"
        border.color: "#3e526dcc"
        border.width: 1
        Text { x: 14; y: 10; text: "FEATURE GATES"; color: "#dbeafe"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.4 }
        Row {
            x: 12
            y: 38
            spacing: 6
            FeatureGate { label: "ADAS"; active: autonomous }
            FeatureGate { label: "NAV"; active: navActive }
            FeatureGate { label: "EV"; active: evActive }
        }
    }

    component FeatureGate: Rectangle {
        property string label: ""
        property bool active: false
        width: 67
        height: 38
        radius: 12
        color: active ? "#0b322dcc" : "#261724cc"
        border.color: active ? "#5eead4" : "#fb7185"
        border.width: 1
        Text { anchors.centerIn: parent; text: label + "\n" + (active ? "ON" : "OFF"); horizontalAlignment: Text.AlignHCenter; color: active ? "#a7f3d0" : "#fecdd3"; font.pixelSize: 9; font.bold: true }
    }

    component SystemStatusCluster: Rectangle {
        property bool autonomous: true
        property bool navActive: true
        property bool evActive: true
        property string adasVersion: "v3.0"
        property string navVersion: "v2.1"
        property string evVersion: "v2.0"
        radius: 12
        color: "#050b14dd"
        border.color: "#344760aa"
        border.width: 1
        clip: true

        Text {
            x: 12
            y: 9
            text: "SYSTEM STATUS"
            color: "#8ea6c7"
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 0.5
        }
        Row {
            x: 12
            y: 30
            spacing: 7
            SystemMiniCard { title: "ADAS"; version: adasVersion; running: autonomous; cpu: autonomous ? "12%" : "4%"; mem: autonomous ? "115 MB" : "86 MB" }
            SystemMiniCard { title: "Navigation"; version: navVersion; running: navActive; cpu: navActive ? "8%" : "3%"; mem: navActive ? "98 MB" : "72 MB" }
            SystemMiniCard { title: "EV Energy"; version: evVersion; running: evActive; cpu: evActive ? "15%" : "5%"; mem: evActive ? "125 MB" : "88 MB" }
        }
    }

    component SystemMiniCard: Rectangle {
        property string title: ""
        property string version: ""
        property bool running: false
        property string cpu: ""
        property string mem: ""
        width: 94
        height: 74
        radius: 8
        color: running ? "#071a19" : "#17111a"
        border.color: running ? "#205d55" : "#593143"
        border.width: 1
        Text { x: 8; y: 7; width: parent.width - 16; elide: Text.ElideRight; text: title; color: "#e5eefb"; font.pixelSize: 9; font.bold: true }
        Text { x: 8; y: 23; text: version; color: "#9fb1ca"; font.pixelSize: 8 }
        Text { x: 8; y: 38; text: running ? "RUNNING" : "DISABLED"; color: running ? "#34d399" : "#fb7185"; font.pixelSize: 8; font.bold: true }
        Text { x: 8; y: 54; text: "CPU " + cpu; color: "#7f93ad"; font.pixelSize: 8 }
        Text { anchors.right: parent.right; anchors.rightMargin: 8; y: 54; text: mem; color: "#7f93ad"; font.pixelSize: 8 }
    }

    component ViewContextPanel: Rectangle {
        property string selectedView: "cockpit"
        property string selectedMenu: "home"
        radius: 16
        color: "#07101bcc"
        border.color: "#3e526d99"
        border.width: 1
        Text {
            x: 14
            y: 9
            text: "ACTIVE HMI LAYER"
            color: "#93a4ba"
            font.pixelSize: 9
            font.bold: true
            font.letterSpacing: 0.6
        }
        Text {
            x: 14
            y: 27
            width: parent.width - 60
            elide: Text.ElideRight
            text: viewText(selectedView) + " / " + menuText(selectedMenu)
            color: "#f8fafc"
            font.pixelSize: 12
            font.bold: true
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 20
            width: 34
            height: 18
            radius: 9
            color: "#0b322d"
            border.color: "#5eead4"
            Text { anchors.centerIn: parent; text: "LIVE"; color: "#99f6e4"; font.pixelSize: 9; font.bold: true }
        }
        function viewText(key) {
            if (key === "surround") return "Surround View"
            if (key === "dashboard") return "Engineering Dashboard"
            if (key === "ops") return "Platform Ops"
            if (key === "logs") return "Event Logs"
            return "First-Person Cockpit"
        }
        function menuText(key) {
            if (key === "vehicle") return "Vehicle"
            if (key === "adas") return "ADAS"
            if (key === "ev") return "EV Energy"
            if (key === "settings") return "Settings"
            return "Home"
        }
    }

    component CockpitInterior: Item {
        property real time: 0
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            y: parent.height * 0.60
            height: parent.height * 0.38
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.24; color: "#070b12ee" }
                GradientStop { position: 1.0; color: "#02040aff" }
            }
        }

        Rectangle {
            x: parent.width * 0.035
            y: parent.height * 0.625
            width: parent.width * 0.93
            height: 28
            radius: 12
            color: "#0a111bcc"
            border.color: "#26364d"
            opacity: 0.92
        }

        Rectangle {
            x: parent.width * 0.31
            y: parent.height * 0.655
            width: parent.width * 0.40
            height: 20
            radius: 9
            color: "#111c2bdd"
            border.color: "#37506d"
            opacity: 0.82
        }

        SteeringWheel {
            x: parent.width * 0.012
            y: parent.height * 0.690
            width: 382
            height: 268
            time: parent.time
        }

        Row {
            x: parent.width * 0.545
            y: parent.height * 0.615
            spacing: 13
            Repeater {
                model: ["P", "R", "N", "D"]
                Text {
                    text: modelData
                    color: modelData === "D" ? "#ffffff" : "#6b7c93"
                    font.pixelSize: 12
                    font.bold: modelData === "D"
                }
            }
        }
    }

    component WindshieldFrame: Item {
        property real time: 0
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                const w = width
                const h = height
                ctx.reset()

                // Top windshield shade and rear-view mirror.
                const top = ctx.createLinearGradient(0, 0, 0, h * 0.18)
                top.addColorStop(0, "rgba(1,4,9,0.92)")
                top.addColorStop(1, "rgba(1,4,9,0.00)")
                ctx.fillStyle = top
                ctx.fillRect(0, 0, w, h * 0.20)

                ctx.fillStyle = "rgba(5,10,18,0.96)"
                roundRect(ctx, w * 0.40, 12, w * 0.20, 30, 8)
                ctx.fill()
                ctx.fillStyle = "rgba(20,34,50,0.78)"
                roundRect(ctx, w * 0.438, 36, w * 0.124, 15, 7)
                ctx.fill()

                // A-pillars as actual cockpit geometry.
                const leftGrad = ctx.createLinearGradient(0, 0, w * 0.18, 0)
                leftGrad.addColorStop(0, "rgba(0,0,0,0.94)")
                leftGrad.addColorStop(1, "rgba(0,0,0,0.00)")
                ctx.fillStyle = leftGrad
                ctx.beginPath()
                ctx.moveTo(0, 0)
                ctx.lineTo(w * 0.12, 0)
                ctx.lineTo(w * 0.045, h * 0.72)
                ctx.lineTo(0, h)
                ctx.closePath()
                ctx.fill()

                const rightGrad = ctx.createLinearGradient(w, 0, w * 0.82, 0)
                rightGrad.addColorStop(0, "rgba(0,0,0,0.94)")
                rightGrad.addColorStop(1, "rgba(0,0,0,0.00)")
                ctx.fillStyle = rightGrad
                ctx.beginPath()
                ctx.moveTo(w, 0)
                ctx.lineTo(w * 0.89, 0)
                ctx.lineTo(w * 0.955, h * 0.72)
                ctx.lineTo(w, h)
                ctx.closePath()
                ctx.fill()

                ctx.strokeStyle = "rgba(202,226,255,0.055)"
                ctx.lineWidth = 1
                for (let i = 0; i < 5; ++i) {
                    ctx.beginPath()
                    ctx.moveTo(w * (0.20 + i * 0.12), h * 0.08)
                    ctx.lineTo(w * (0.14 + i * 0.14), h * 0.56)
                    ctx.stroke()
                }
            }
        }
    }

    component SteeringWheel: Item {
        property real time: 0
        Canvas {
            anchors.fill: parent
            property real localTime: parent.time
            onLocalTimeChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                const w = width
                const h = height
                ctx.reset()
                const cx = w * 0.50
                const cy = h * 0.55
                const wobble = Math.sin(localTime * 0.85) * 0.05
                ctx.translate(cx, cy)
                ctx.rotate(wobble)

                const rim = ctx.createRadialGradient(0, 0, 40, 0, 0, 150)
                rim.addColorStop(0.00, "rgba(8,12,18,0)")
                rim.addColorStop(0.58, "rgba(11,17,25,0)")
                rim.addColorStop(0.70, "#2c3748")
                rim.addColorStop(0.82, "#070b12")
                rim.addColorStop(1.00, "rgba(0,0,0,0)")
                ctx.fillStyle = rim
                ctx.beginPath()
                ctx.arc(0, 0, 143, 0, Math.PI * 2)
                ctx.fill()

                ctx.strokeStyle = "#111927"
                ctx.lineWidth = 18
                ctx.beginPath()
                ctx.arc(0, 0, 128, 0.22 * Math.PI, 1.86 * Math.PI)
                ctx.stroke()

                ctx.strokeStyle = "#252f40"
                ctx.lineWidth = 10
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(-95, 22)
                ctx.quadraticCurveTo(-52, 24, -24, 10)
                ctx.moveTo(95, 22)
                ctx.quadraticCurveTo(52, 24, 24, 10)
                ctx.moveTo(0, 12)
                ctx.lineTo(0, 118)
                ctx.stroke()

                const hub = ctx.createLinearGradient(-60, -38, 60, 42)
                hub.addColorStop(0, "#1c2736")
                hub.addColorStop(1, "#0b111b")
                ctx.fillStyle = hub
                roundRect(ctx, -66, -38, 132, 78, 22)
                ctx.fill()
                ctx.strokeStyle = "#344256"
                ctx.lineWidth = 1.5
                ctx.stroke()

                ctx.strokeStyle = "#8b98aa"
                ctx.lineWidth = 3
                ctx.save()
                ctx.scale(1.75, 0.78)
                ctx.beginPath()
                ctx.arc(0, -8, 34, 0, Math.PI * 2)
                ctx.stroke()
                ctx.restore()
                ctx.strokeStyle = "#b8c2d0"
                ctx.lineWidth = 4
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(-23, 4)
                ctx.lineTo(-9, -22)
                ctx.moveTo(7, 20)
                ctx.lineTo(22, -6)
                ctx.moveTo(-10, -2)
                ctx.lineTo(13, -8)
                ctx.stroke()
            }
        }
    }

    component InstrumentCluster: Rectangle {
        property real time: 0
        property real speedKph: 72
        property bool autonomous: true
        property string adasVersion: "v3.0"
        radius: 18
        color: "#08101ccc"
        border.color: "#5c6f8788"
        border.width: 1
        Text { x: 24; y: 18; text: Math.round(speedKph); color: "#ffffff"; font.pixelSize: 42; font.bold: true }
        Text { x: 98; y: 45; text: "km/h"; color: "#cbd5e1"; font.pixelSize: 13 }
        Text { x: 24; y: 82; width: 162; elide: Text.ElideRight; text: (autonomous ? "AUTO LKA ACC FCA" : "ADAS STANDBY") + " " + adasVersion; color: autonomous ? "#5eead4" : "#fbbf24"; font.pixelSize: 11; font.bold: true }
        Canvas {
            x: parent.width - 126; y: 18; width: 108; height: 96
            property bool localAutonomous: parent.autonomous
            onLocalAutonomousChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = localAutonomous ? "#5eead4" : "#64748b"; ctx.lineWidth = 2
                ctx.beginPath(); ctx.moveTo(24, 90); ctx.lineTo(50, 15); ctx.moveTo(108, 90); ctx.lineTo(82, 15); ctx.stroke()
            }
        }
        Rectangle { x: parent.width - 78; y: 46; width: 28; height: 48; radius: 7; color: autonomous ? "#e2e8f0" : "#94a3b8"; border.color: "#0f172a" }
    }

    component NavigationCard: Rectangle {
        property real time: 0
        property bool navActive: true
        property string navVersion: "v2.1"
        radius: 18
        color: "#08101ccc"
        border.color: navActive ? "#5c6f8788" : "#92400e88"
        border.width: 1
        Text { x: 20; y: 18; text: "↱"; color: navActive ? "#60a5fa" : "#64748b"; font.pixelSize: 34; font.bold: true }
        Text { x: 74; y: 22; text: navActive ? "120 m" : "NAV OFF"; color: "#ffffff"; font.pixelSize: 19; font.bold: true }
        Text { x: 74; y: 53; width: parent.width - 92; elide: Text.ElideRight; text: navActive ? "판교 / 분당 방면  " + navVersion : "Navigation feature disabled"; color: "#cbd5e1"; font.pixelSize: 12 }
        Rectangle { x: 20; y: 87; width: parent.width - 40; height: 4; radius: 2; color: navActive ? "#38bdf8" : "#64748b"; opacity: 0.75 + Math.sin(time * 2.2) * 0.18 }
    }

    component EvCard: Rectangle {
        property real time: 0
        property int batteryPct: 82
        property int rangeKm: 312
        property bool evActive: true
        property string evVersion: "v2.0"
        radius: 18
        color: "#08101ccc"
        border.color: evActive ? "#5c6f8788" : "#7c2d1288"
        border.width: 1
        Text { x: 20; y: 16; text: "EV  " + evVersion; color: evActive ? "#99f6e4" : "#fb923c"; font.pixelSize: 13; font.bold: true }
        Text { x: 20; y: 42; text: "Battery"; color: "#94a3b8"; font.pixelSize: 11 }
        Text { x: 92; y: 38; text: batteryPct + "%"; color: "#ffffff"; font.pixelSize: 18; font.bold: true }
        Rectangle { x: 20; y: 72; width: parent.width - 48; height: 9; radius: 5; color: "#172033" }
        Rectangle { x: 20; y: 72; width: (parent.width - 48) * Math.max(0, Math.min(100, batteryPct)) / 100; height: 9; radius: 5; color: evActive ? "#2dd4bf" : "#64748b" }
        Text { x: 20; y: 91; text: "Range"; color: "#94a3b8"; font.pixelSize: 11 }
        Text { x: 92; y: 87; text: rangeKm + " km"; color: "#e2e8f0"; font.pixelSize: 15; font.bold: true }
    }

    component TopBar: Rectangle {
        id: topBarRoot
        property string vehicleId: "car_1"
        property bool mqttConnected: false
        property string selectedView: "cockpit"
        signal viewSelected(string view)
        signal carPicked(string carId)
        radius: 0
        color: "#070c14"
        border.color: "#1d2b3d"
        border.width: 1

        HyundaiMark {
            x: 12
            y: 8
            width: 30
            height: 24
        }
        Text {
            x: 56
            y: 12
            width: 238
            elide: Text.ElideRight
            text: "Hyundai SDV Cockpit HMI"
            color: "#f2f7ff"
            font.pixelSize: 13
            font.bold: true
        }
        Rectangle {
            x: 298
            y: 11
            width: 8
            height: 8
            radius: 4
            color: mqttConnected ? "#22c55e" : "#f97316"
        }
        CarPicker {
            x: 318
            y: 7
            width: 128
            height: 28
            currentCar: topBarRoot.vehicleId
            onPicked: function(carId) { topBarRoot.carPicked(carId) }
        }
        Row {
            x: 468
            y: 7
            spacing: 8
            TopChip { key: "cockpit"; text: "1인칭"; active: topBarRoot.selectedView === key; onActivated: function(view) { topBarRoot.viewSelected(view) } }
            TopChip { key: "surround"; text: "3인칭"; active: topBarRoot.selectedView === key; onActivated: function(view) { topBarRoot.viewSelected(view) } }
            TopChip { key: "dashboard"; text: "대시보드"; active: topBarRoot.selectedView === key; onActivated: function(view) { topBarRoot.viewSelected(view) } }
            TopChip { key: "ops"; text: "운영"; active: topBarRoot.selectedView === key; onActivated: function(view) { topBarRoot.viewSelected(view) } }
            TopChip { key: "logs"; text: "로그"; active: topBarRoot.selectedView === key; onActivated: function(view) { topBarRoot.viewSelected(view) } }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 13
            text: mqttConnected ? "LIVE MQTT / OTA" : "HTTP POLL / MQTT OFF"
            color: "#7f8fa6"
            font.pixelSize: 10
        }
    }

    component CarPicker: Rectangle {
        id: carPickerRoot
        property string currentCar: "car_1"
        property bool opened: false
        signal picked(string carId)
        radius: 8
        color: "#0c1420"
        border.color: opened ? "#60a5fa" : "#24344a"
        border.width: 1
        z: opened ? 100 : 1

        Text {
            x: 12
            y: 7
            text: currentCar
            color: "#eaf4ff"
            font.pixelSize: 11
            font.bold: true
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 7
            text: opened ? "▲" : "▼"
            color: "#8aa0ba"
            font.pixelSize: 9
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: carPickerRoot.opened = !carPickerRoot.opened
        }

        Rectangle {
            x: 0
            y: parent.height + 4
            width: parent.width
            height: opened ? 244 : 0
            visible: opened
            radius: 10
            color: "#07101bee"
            border.color: "#36506e"
            border.width: 1
            clip: true
            z: 101
            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                Repeater {
                    model: 10
                    Rectangle {
                        width: parent.width
                        height: 21
                        radius: 6
                        color: carPickerRoot.currentCar === ("car_" + (index + 1)) ? "#0c4f95" : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "car_" + (index + 1)
                            color: "#dbeafe"
                            font.pixelSize: 10
                            font.bold: carPickerRoot.currentCar === text
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const nextCar = "car_" + (index + 1)
                                carPickerRoot.opened = false
                                carPickerRoot.picked(nextCar)
                            }
                        }
                    }
                }
            }
        }
    }

    component HyundaiMark: Item {
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width
                const h = height
                ctx.fillStyle = "#0d1725"
                roundRect(ctx, 0, 0, w, h, 12)
                ctx.fill()
                ctx.strokeStyle = "#60738c"
                ctx.lineWidth = 1
                ctx.stroke()
                ctx.save()
                ctx.translate(w / 2, h / 2)
                ctx.scale(1.25, 0.72)
                ctx.strokeStyle = "#f8fbff"
                ctx.lineWidth = 1.7
                ctx.beginPath()
                ctx.arc(0, 0, 10, 0, Math.PI * 2)
                ctx.stroke()
                ctx.restore()
                ctx.strokeStyle = "#f8fbff"
                ctx.lineWidth = 2.2
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.moveTo(w * 0.32, h * 0.70)
                ctx.lineTo(w * 0.43, h * 0.28)
                ctx.moveTo(w * 0.58, h * 0.72)
                ctx.lineTo(w * 0.70, h * 0.30)
                ctx.moveTo(w * 0.42, h * 0.50)
                ctx.lineTo(w * 0.61, h * 0.44)
                ctx.stroke()
            }
        }
    }

    component TopChip: Rectangle {
        property string key: ""
        property string text: ""
        property bool active: false
        signal activated(string key)
        width: active ? 76 : 70
        height: 27
        radius: 7
        color: active ? "#0c4f95" : "#0c1420"
        border.color: active ? "#2563eb" : "#24344a"
        Text {
            anchors.centerIn: parent
            text: parent.text
            color: parent.active ? "#eaf4ff" : "#9aa9bc"
            font.pixelSize: 11
            font.bold: parent.active
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.activated(parent.key)
        }
    }

    component SideMenu: Rectangle {
        id: sideMenuRoot
        property string selectedMenu: "home"
        signal menuSelected(string menu)
        color: "#060b12"
        border.color: "#1c2a3d"
        border.width: 1

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            spacing: 8
            SideButton { key: "home"; icon: "⌂"; label: "홈"; active: sideMenuRoot.selectedMenu === key; onActivated: function(menu) { sideMenuRoot.menuSelected(menu) } }
            SideButton { key: "vehicle"; icon: "▦"; label: "차량"; active: sideMenuRoot.selectedMenu === key; onActivated: function(menu) { sideMenuRoot.menuSelected(menu) } }
            SideButton { key: "adas"; icon: "◎"; label: "ADAS"; active: sideMenuRoot.selectedMenu === key; onActivated: function(menu) { sideMenuRoot.menuSelected(menu) } }
            SideButton { key: "ev"; icon: "⌁"; label: "EV"; active: sideMenuRoot.selectedMenu === key; onActivated: function(menu) { sideMenuRoot.menuSelected(menu) } }
            SideButton { key: "settings"; icon: "⚙"; label: "설정"; active: sideMenuRoot.selectedMenu === key; onActivated: function(menu) { sideMenuRoot.menuSelected(menu) } }
        }
    }

    component SideButton: Rectangle {
        property string key: ""
        property string icon: ""
        property string label: ""
        property bool active: false
        signal activated(string key)
        width: 44
        height: 55
        radius: 10
        color: active ? "#10233a" : "#08121f"
        border.color: active ? "#2563eb" : "#24344a"
        Column {
            anchors.centerIn: parent
            spacing: 2
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: icon; color: active ? "#60a5fa" : "#91a0b6"; font.pixelSize: 15; font.bold: true }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: label; color: "#8493a8"; font.pixelSize: 9 }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.activated(parent.key)
        }
    }

    function lerp(a, b, p) { return a + (b - a) * p }
    function pointLerp(a, b, p) { return { "x": lerp(a.x, b.x, p), "y": lerp(a.y, b.y, p) } }
    function viewLabel(key) {
        if (key === "surround") return "Surround View"
        if (key === "dashboard") return "Engineering Dashboard"
        if (key === "ops") return "Platform Ops"
        if (key === "logs") return "Event Logs"
        return "First-Person Cockpit"
    }
    function menuLabel(key) {
        if (key === "vehicle") return "Vehicle"
        if (key === "adas") return "ADAS"
        if (key === "ev") return "EV Energy"
        if (key === "settings") return "Settings"
        return "Home"
    }
    function roundRect(ctx, x, y, w, h, r) {
        ctx.beginPath()
        ctx.moveTo(x + r, y)
        ctx.lineTo(x + w - r, y)
        ctx.quadraticCurveTo(x + w, y, x + w, y + r)
        ctx.lineTo(x + w, y + h - r)
        ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h)
        ctx.lineTo(x + r, y + h)
        ctx.quadraticCurveTo(x, y + h, x, y + h - r)
        ctx.lineTo(x, y + r)
        ctx.quadraticCurveTo(x, y, x + r, y)
        ctx.closePath()
    }
    function drawLane(ctx, near, far, color, width) {
        ctx.lineCap = "round"
        ctx.strokeStyle = color
        ctx.lineWidth = width + 12
        ctx.globalAlpha = 0.20
        ctx.beginPath()
        ctx.moveTo(near.x, near.y)
        ctx.bezierCurveTo(lerp(near.x, far.x, 0.35), lerp(near.y, far.y, 0.35), lerp(near.x, far.x, 0.72), lerp(near.y, far.y, 0.72), far.x, far.y)
        ctx.stroke()
        ctx.globalAlpha = 1.0
        ctx.strokeStyle = color
        ctx.lineWidth = width
        ctx.beginPath()
        ctx.moveTo(near.x, near.y)
        ctx.bezierCurveTo(lerp(near.x, far.x, 0.35), lerp(near.y, far.y, 0.35), lerp(near.x, far.x, 0.72), lerp(near.y, far.y, 0.72), far.x, far.y)
        ctx.stroke()
    }
}
