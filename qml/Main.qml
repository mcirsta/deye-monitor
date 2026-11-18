import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 1380
    height: 940
    minimumWidth: 1080
    minimumHeight: 800
    visible: true
    title: "Deye · Local energy"
    color: "#f2f6f3"
    font.family: "Inter"
    property int page: 0
    readonly property bool hasBatteryPower: monitor.values.bat_power !== undefined
    readonly property int batteryDirection: !hasBatteryPower ? 0 :
        monitor.values.bat_power < -30 ? -1 : monitor.values.bat_power > 30 ? 1 : 0
    readonly property color batteryStateColor: batteryDirection < 0 ? "#168466" :
        batteryDirection > 0 ? "#b76324" : "#607772"
    function power(key) {
        let n = monitor.values[key];
        return n === undefined ? "—" : (Math.abs(n) / 1000).toFixed(2);
    }
    function number(key, digits) {
        let n = monitor.values[key];
        return n === undefined ? "—" : Number(n).toFixed(digits);
    }
    function energyNote(value, label) {
        return typeof value === "number" && Number.isFinite(value) && value >= 0
            ? value.toFixed(1) + " kWh " + label : "";
    }
    function openSettings() {
        settingsWindow.load();
        settingsWindow.show();
        settingsWindow.raise();
        settingsWindow.requestActivate();
    }
    component ActionButton: Button {
        property string iconName
        implicitWidth: 38
        implicitHeight: 38
        background: Rectangle {
            color: parent.hovered ? "#e7eee9" : "white"
            radius: 10
            border.color: "#dce6df"
        }
        contentItem: Icon {
            name: parent.iconName
            color: parent.enabled ? "#45665b" : "#b0bcb6"
            width: 18
            height: 18
        }
    }
    SettingsWindow {
        id: settingsWindow
        transientParent: root
    }
    ProbeWindow {
        id: probeWindow
        transientParent: root
    }
    Rectangle {
        id: sidebar
        width: 200
        readonly property color mutedText: "#aac4b8"
        anchors {
            top: parent.top
            bottom: parent.bottom
            left: parent.left
        }
        color: "#112d2b"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 7
            RowLayout {
                Layout.topMargin: 17
                Layout.bottomMargin: 4
                spacing: 11
                Image {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    Layout.fillWidth: false
                    Layout.fillHeight: false
                    source: "qrc:/assets/com.deye.monitor.svg"
                    sourceSize.width: 38 * Screen.devicePixelRatio
                    sourceSize.height: 38 * Screen.devicePixelRatio
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    text: "deye"
                    font.pixelSize: 29
                    font.weight: Font.DemiBold
                    font.letterSpacing: -.5
                    color: "#f4faf6"
                }
            }
            Text {
                text: "LOCAL ENERGY"
                font.pixelSize: 11
                font.letterSpacing: 1.6
                color: sidebar.mutedText
                Layout.leftMargin: 3
                Layout.bottomMargin: 42
            }
            Repeater {
                model: [
                    {
                        label: "Overview",
                        icon: "overview"
                    },
                    {
                        label: "Telemetry",
                        icon: "inverter"
                    },
                    {
                        label: "History",
                        icon: "chart"
                    }
                ]
                delegate: Button {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 45
                    onClicked: root.page = index
                    background: Rectangle {
                        radius: 9
                        color: root.page === index ? "#234c40" : parent.hovered ? "#193d35" : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        Icon {
                            Layout.leftMargin: 9
                            Layout.preferredWidth: 19
                            Layout.preferredHeight: 19
                            name: modelData.icon
                            color: root.page === index ? "#8cddb8" : "#7fa394"
                        }
                        Text {
                            text: modelData.label
                            font.pixelSize: 13
                            font.weight: root.page === index ? Font.DemiBold : Font.Normal
                            color: root.page === index ? "#e3f7e9" : sidebar.mutedText
                            Layout.fillWidth: true
                        }
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 113
                radius: 11
                color: "#193d34"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 7
                    RowLayout {
                        spacing: 7
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            color: monitor.connected ? "#71d7a3" : monitor.demo ? "#e9a23b" : "#99aaa2"
                        }
                        Text {
                            text: monitor.demo ? "DEMO PREVIEW" : monitor.connected ? "LOCAL CONNECTION" : "OFFLINE"
                            color: sidebar.mutedText
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            font.letterSpacing: .5
                        }
                    }
                    Text {
                        text: monitor.host || "No logger selected"
                        color: "#daeee1"
                        font.pixelSize: 14
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Text {
                        text: monitor.demo ? "Sample data" : "Read-only monitoring"
                        color: sidebar.mutedText
                        font.pixelSize: 12
                    }
                }
            }
            Button {
                Layout.fillWidth: true
                implicitHeight: 45
                Layout.topMargin: 12
                onClicked: root.openSettings()
                background: Rectangle {
                    radius: 9
                    color: parent.hovered ? "#234c40" : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 12
                    Icon {
                        Layout.leftMargin: 9
                        Layout.preferredWidth: 19
                        Layout.preferredHeight: 19
                        name: "settings"
                        color: "#7fa394"
                    }
                    Text {
                        text: "Settings"
                        font.pixelSize: 13
                        color: sidebar.mutedText
                        Layout.fillWidth: true
                    }
                }
            }
            Text {
                text: "DESKTOP  /  2.0"
                font.pixelSize: 10
                font.letterSpacing: 1
                color: sidebar.mutedText
                Layout.leftMargin: 10
                Layout.topMargin: 10
                Layout.bottomMargin: 5
            }
        }
    }
    ColumnLayout {
        anchors {
            left: sidebar.right
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            margins: 28
        }
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 7
                Text {
                    text: ["Energy overview", "Inverter telemetry", "Session history"][root.page]
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    font.letterSpacing: -.6
                    color: "#183d37"
                }
                Text {
                    text: monitor.model + (monitor.serial ? "  ·  " + monitor.serial : "  ·  Select a logger to get started")
                    font.pixelSize: 13
                    color: "#778780"
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Rectangle {
                implicitWidth: statusText.implicitWidth + 27
                implicitHeight: 29
                radius: 15
                color: monitor.demo ? "#fff2d9" : monitor.connected ? "#e1f2e7" : "#e6ebe7"
                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: monitor.demo ? "Demo preview" : monitor.paused ? "Paused" : monitor.connected ? "●  Live" : "●  Offline"
                    color: monitor.demo ? "#a8751f" : monitor.connected ? "#2c8f64" : "#7d8b80"
                    font.pixelSize: 11
                }
            }
            ActionButton {
                iconName: monitor.paused ? "play" : "pause"
                enabled: !monitor.demo
                onClicked: monitor.togglePaused()
                ToolTip.visible: hovered
                ToolTip.text: monitor.paused ? "Resume monitoring" : "Pause monitoring"
            }
            ActionButton {
                iconName: "refresh"
                enabled: !monitor.busy && !monitor.demo
                onClicked: monitor.refresh()
                ToolTip.visible: hovered
                ToolTip.text: "Refresh now"
            }
        }
        Rectangle {
            objectName: "diagnosticOfferBanner"
            visible: !monitor.connected && !monitor.demo
            Layout.fillWidth: true
            implicitHeight: noticeRow.implicitHeight + 24
            radius: 10
            color: "#fff3dc"
            border.color: "#f3e4c6"
            RowLayout {
                id: noticeRow
                anchors.fill: parent
                anchors.margins: 12
                spacing: 14
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: monitor.status
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: "#8b6d3b"
                    }
                    Text {
                        visible: monitor.probeOffered
                        Layout.fillWidth: true
                        text: "Startup could not verify support for this device. Save the probe data and send it to the maintainer to help investigate."
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: "#8b6d3b"
                    }
                }
                Button {
                    objectName: "probeOfferButton"
                    visible: monitor.probeOffered
                    text: "Save probe report…"
                    implicitHeight: 36
                    leftPadding: 14
                    rightPadding: 14
                    onClicked: probeWindow.open()
                    background: Rectangle {
                        radius: 8
                        color: parent.hovered ? "#ffefcb" : "white"
                        border.color: "#e7d4ae"
                    }
                    contentItem: Text {
                        text: parent.text
                        font.pixelSize: 12
                        color: "#8b6d3b"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ToolButton {
                    objectName: "dismissProbeOffer"
                    visible: monitor.probeOffered
                    text: "×"
                    font.pixelSize: 20
                    onClicked: monitor.dismissProbeOffer()
                    ToolTip.visible: hovered
                    ToolTip.text: "Dismiss report offer"
                }
            }
        }
        Item {
            id: overviewViewport
            visible: root.page === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            ScrollView {
                id: overviewScroll
                anchors.fill: parent
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth
                contentHeight: overviewContent.height
                ColumnLayout {
                    id: overviewContent
                    width: overviewScroll.availableWidth
                    height: Math.max(740, overviewViewport.height)
                    spacing: 18
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        MetricCard {
                            objectName: "solarProductionCard"
                            Layout.fillWidth: true
                            title: "Solar production"
                            icon: "sun"
                            accent: "#e9a23b"
                            value: root.power("pv_total_p")
                            note: root.energyNote(monitor.values.e_today_pv, "generated today")
                        }
                        MetricCard {
                            objectName: "homeConsumptionCard"
                            Layout.fillWidth: true
                            title: "Home consumption"
                            icon: "home"
                            accent: "#5485c0"
                            value: root.power("load_power")
                            note: root.energyNote(monitor.values.e_today_load, "consumed today")
                        }
                        MetricCard {
                            objectName: "batteryPowerCard"
                            Layout.fillWidth: true
                            title: !root.hasBatteryPower ? "Battery power" :
                                root.batteryDirection < 0 ? "Charging battery" :
                                root.batteryDirection > 0 ? "Discharging battery" : "Battery idle"
                            icon: "battery"
                            accent: root.batteryStateColor
                            titleColor: root.batteryStateColor
                            valueColor: root.batteryDirection > 0 ? "#183d37" : root.batteryStateColor
                            emphasizeTitle: true
                            wrapTitle: true
                            value: root.power("bat_power")
                            note: !root.hasBatteryPower ? "Waiting for telemetry" :
                                root.batteryDirection < 0 ? "Power into battery" :
                                root.batteryDirection > 0 ? "Power from battery" : "No significant battery flow"
                        }
                        MetricCard {
                            Layout.fillWidth: true
                            title: "Grid exchange"
                            icon: "grid"
                            accent: "#9679bc"
                            value: root.power("grid_power")
                            note: monitor.values.grid_power === undefined ? "Waiting for telemetry" : monitor.values.grid_power < 0 ? "Exporting to the grid" : monitor.values.grid_power > 0 ? "Importing from the grid" : "No grid exchange"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: 325
                        Layout.minimumHeight: 325
                        spacing: 18
                        FlowView {
                            objectName: "energyFlowView"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 650
                            values: monitor.values
                            batteryDirection: root.batteryDirection
                            batteryColor: root.batteryStateColor
                            preview: monitor.demo
                            active: (monitor.connected && !monitor.paused) || monitor.demo
                            animationsEnabled: root.page === 0 && root.visible && root.active &&
                                root.visibility !== Window.Minimized
                            workMode: monitor.states.work_mode || "Waiting for telemetry"
                        }
                        Rectangle {
                            Layout.preferredWidth: 279
                            Layout.fillHeight: true
                            color: "white"
                            radius: 16
                            border.color: "#e0e8e4"
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 22
                                spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: "Battery"
                                        font.pixelSize: 16
                                        font.weight: Font.DemiBold
                                        color: "#183d37"
                                        Layout.fillWidth: true
                                    }
                                    Icon {
                                        name: "battery"
                                        color: "#21a482"
                                        Layout.preferredWidth: 21
                                        Layout.preferredHeight: 21
                                    }
                                }
                                Text {
                                    text: "Inverter-reported SOC"
                                    color: "#778780"
                                    font.pixelSize: 12
                                }
                                Row {
                                    spacing: 5
                                    Layout.topMargin: 7
                                    Text {
                                        text: root.number("bat_soc", 0)
                                        font.pixelSize: 47
                                        font.weight: Font.DemiBold
                                        color: "#183d37"
                                    }
                                    Text {
                                        text: "%"
                                        font.pixelSize: 19
                                        color: "#778780"
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 9
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 7
                                    radius: 4
                                    color: "#eaf2ed"
                                    Rectangle {
                                        height: parent.height
                                        width: parent.width * Math.min(100, Math.max(0, monitor.values.bat_soc || 0)) / 100
                                        radius: 4
                                        color: "#2dac85"
                                    }
                                }
                                Text {
                                    objectName: "batteryStateText"
                                    text: !root.hasBatteryPower ? "Waiting for telemetry" :
                                        root.batteryDirection < 0 ? "Charging" :
                                        root.batteryDirection > 0 ? "Discharging" : "Idle"
                                    color: root.batteryStateColor
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    visible: root.batteryDirection < 0
                                    text: monitor.chargeEta || "Learning charge trend…"
                                    color: "#778780"
                                    font.pixelSize: 13
                                    Layout.bottomMargin: 5
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1
                                    color: "#edf1ee"
                                }
                                Repeater {
                                    model: [
                                        {
                                            label: "Voltage",
                                            key: "bat_v",
                                            unit: "V"
                                        },
                                        {
                                            label: "Current",
                                            key: "bat_i",
                                            unit: "A"
                                        },
                                        {
                                            label: "Temperature",
                                            key: "bat_temp",
                                            unit: "°C"
                                        }
                                    ]
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Text {
                                            text: modelData.label
                                            color: "#778780"
                                            font.pixelSize: 13
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: root.number(modelData.key, 1) + " " + modelData.unit
                                            color: "#315347"
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                                Item {
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }
                    HistoryPanel {
                        objectName: "overviewHistoryPanel"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 235
                        Layout.minimumHeight: 215
                        samples: monitor.history
                    }
                }
            }
        }
        DetailView {
            visible: root.page === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            groups: monitor.detailGroups
        }
        HistoryPanel {
            objectName: "sessionHistoryPanel"
            visible: root.page === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: monitor.history
            minutes: 60
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                width: 5
                height: 5
                radius: 3
                color: monitor.connected ? "#39a67e" : "#a4b0a7"
            }
            Text {
                text: monitor.status
                color: "#778780"
                font.pixelSize: 12
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: (monitor.profile || "Auto profile") + "  ·  Updated " + monitor.updated
                color: "#778780"
                font.pixelSize: 12
            }
        }
    }
}
