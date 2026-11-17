import QtQuick
import QtQuick.Layouts

Rectangle {
    id: flow
    required property var values
    property bool active: false
    property bool animationsEnabled: true
    property bool preview: false
    property int batteryDirection: 0
    property string workMode: "Waiting for telemetry"
    property color batteryColor: "#607772"
    readonly property bool hasMovingPower:
        Math.abs(values.pv_total_p || 0) > 30 ||
        Math.abs(values.grid_power || 0) > 30 ||
        Math.abs(values.bat_power || 0) > 30 ||
        Math.abs(values.load_power || 0) > 30
    color: "white"
    radius: 16
    border.color: "#e0e8e4"
    function power(key) {
        return values[key] === undefined ? "—" : (Math.abs(values[key]) / 1000).toFixed(2) + " kW";
    }
    Text {
        x: 22
        y: 21
        text: "Power flow"
        font.pixelSize: 16
        font.weight: Font.DemiBold
        color: "#183d37"
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 22
        y: 24
        text: flow.preview ? "PREVIEW" : flow.active ? "LIVE" : "INACTIVE"
        color: flow.active ? "#1c9376" : "#a1aaa6"
        font.pixelSize: 10
        font.letterSpacing: 1.5
    }
    Item {
        id: diagram
        objectName: "powerFlowDiagram"
        width: Math.max(0, Math.min(parent.width - 44, 900))
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: parent.top
            bottom: parent.bottom
            topMargin: 62
            bottomMargin: 36
        }
        readonly property real leftFraction: .18
        readonly property real rightFraction: .82
        readonly property real topFraction: height > 0 ? Math.max(36, height * .1) / height : .16
        readonly property real bottomFraction: height > 0 ?
            (height - Math.max(75, height * .1)) / height : .65
        readonly property real centerFraction: (topFraction + bottomFraction) / 2
        readonly property real iconRadius: 21
        readonly property real inverterRadius: 43
        readonly property real cx: width * .5
        readonly property real cy: height * centerFraction
        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                let c = getContext("2d");
                c.reset();
                c.strokeStyle = "#dce9e3";
                c.lineWidth = 2;
                c.beginPath();
                const left = width * diagram.leftFraction;
                const right = width * diagram.rightFraction;
                const top = height * diagram.topFraction;
                const bottom = height * diagram.bottomFraction;
                const middle = height * diagram.centerFraction;
                c.moveTo(left + diagram.iconRadius, top);
                c.lineTo(right - diagram.iconRadius, top);
                c.moveTo(left + diagram.iconRadius, bottom);
                c.lineTo(right - diagram.iconRadius, bottom);
                c.moveTo(diagram.cx, top);
                c.lineTo(diagram.cx, middle - diagram.inverterRadius);
                c.moveTo(diagram.cx, middle + diagram.inverterRadius);
                c.lineTo(diagram.cx, bottom);
                c.stroke();
            }
        }
        Repeater {
            model: [
                {
                    k: "pv_total_p",
                    title: "Solar",
                    icon: "sun",
                    color: "#e9a23b",
                    x: diagram.leftFraction,
                    y: diagram.topFraction
                },
                {
                    k: "grid_power",
                    title: "Grid",
                    icon: "grid",
                    color: "#9679bc",
                    x: diagram.rightFraction,
                    y: diagram.topFraction
                },
                {
                    k: "bat_power",
                    title: "Battery",
                    icon: "battery",
                    color: flow.batteryColor,
                    x: diagram.leftFraction,
                    y: diagram.bottomFraction
                },
                {
                    k: "load_power",
                    title: "Home",
                    icon: "home",
                    color: "#5485c0",
                    x: diagram.rightFraction,
                    y: diagram.bottomFraction
                }
            ]
            delegate: Item {
                id: node
                required property var modelData
                width: 112
                height: childrenRect.height
                z: 1
                x: diagram.width * modelData.x - width / 2
                y: diagram.height * modelData.y - diagram.iconRadius
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4
                    Rectangle {
                        objectName: node.modelData.k + "FlowIcon"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 42
                        height: 42
                        radius: 13
                        color: "#f6f9f7"
                        border.color: "#e4ece7"
                        Icon {
                            anchors.centerIn: parent
                            width: 24
                            height: 24
                            name: node.modelData.icon
                            color: node.modelData.color
                        }
                    }
                    Text {
                        objectName: node.modelData.k + "FlowLabel"
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: node.modelData.title
                        color: "#7a8e86"
                        font.pixelSize: 11
                    }
                    Text {
                        objectName: node.modelData.k + "FlowValue"
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: flow.power(node.modelData.k)
                        color: node.modelData.k === "bat_power" && flow.batteryDirection <= 0 ?
                            flow.batteryColor : "#234339"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
        Repeater {
            model: ["pv_total_p", "grid_power", "bat_power", "load_power"]
            delegate: Rectangle {
                id: dot
                required property string modelData
                objectName: modelData + "FlowDot"
                readonly property bool motionRunning: motion.running
                readonly property bool reverse: modelData === "load_power" ||
                    (modelData === "grid_power" && flow.values.grid_power < 0) ||
                    (modelData === "bat_power" && flow.batteryDirection < 0)
                readonly property real columnFraction:
                    modelData === "grid_power" || modelData === "load_power" ?
                        diagram.rightFraction : diagram.leftFraction
                readonly property real rowFraction:
                    modelData === "bat_power" || modelData === "load_power" ?
                        diagram.bottomFraction : diagram.topFraction
                readonly property real startX: diagram.width * columnFraction - width / 2
                readonly property real startY: diagram.height * rowFraction - height / 2
                readonly property real centerX: diagram.cx - width / 2
                readonly property real centerY: diagram.cy - height / 2
                width: 6
                height: 6
                radius: 3
                color: modelData === "pv_total_p" ? "#e9a23b" :
                    modelData === "grid_power" ? "#9679bc" :
                    modelData === "bat_power" ? flow.batteryColor : "#5485c0"
                visible: flow.active && Math.abs(flow.values[modelData] || 0) > 30
                x: reverse ? centerX : startX
                y: reverse ? centerY : startY
                // These animators move the scene graph node without notifying QML every frame.
                // The two legs retain the original 65%/35% split of the 2400 ms cycle.
                SequentialAnimation {
                    id: motion
                    loops: Animation.Infinite
                    running: flow.active && flow.animationsEnabled && flow.hasMovingPower
                    ParallelAnimation {
                        XAnimator {
                            target: dot
                            from: dot.reverse ? dot.centerX : dot.startX
                            to: dot.centerX
                            duration: dot.reverse ? 840 : 1560
                            easing.type: Easing.Linear
                        }
                        YAnimator {
                            target: dot
                            from: dot.reverse ? dot.centerY : dot.startY
                            to: dot.startY
                            duration: dot.reverse ? 840 : 1560
                            easing.type: Easing.Linear
                        }
                    }
                    ParallelAnimation {
                        XAnimator {
                            target: dot
                            from: dot.centerX
                            to: dot.reverse ? dot.startX : dot.centerX
                            duration: dot.reverse ? 1560 : 840
                            easing.type: Easing.Linear
                        }
                        YAnimator {
                            target: dot
                            from: dot.startY
                            to: dot.reverse ? dot.startY : dot.centerY
                            duration: dot.reverse ? 1560 : 840
                            easing.type: Easing.Linear
                        }
                    }
                }
                onReverseChanged: {
                    if (motion.running)
                        motion.restart();
                }
                Connections {
                    target: diagram
                    function onWidthChanged() {
                        if (motion.running)
                            motion.restart();
                    }
                    function onHeightChanged() {
                        if (motion.running)
                            motion.restart();
                    }
                }
            }
        }
        Rectangle {
            objectName: "inverterFlowBadge"
            z: 2
            x: diagram.cx - 43
            y: diagram.cy - 43
            width: 86
            height: 86
            radius: 24
            color: "#edf7f2"
            border.color: "#c9e5d8"
            Column {
                anchors.centerIn: parent
                spacing: 7
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 29
                    height: 29
                    name: "inverter"
                    color: "#008b70"
                }
                Text {
                    text: "INVERTER"
                    font.pixelSize: 8
                    font.letterSpacing: 1
                    color: "#38836d"
                }
            }
        }
    }
    Text {
        objectName: "powerFlowMode"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 15
        text: flow.workMode
        color: "#83958d"
        font.pixelSize: 10
    }
}
