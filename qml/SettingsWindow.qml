import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: config
    objectName: "settingsWindow"
    width: 620
    height: 740
    minimumWidth: 550
    minimumHeight: 640
    title: "Deye · Settings"
    color: "#f2f6f3"
    visible: false
    font.family: "Inter"
    property string scanMessage: "Scan finds Solarman loggers on the local network."
    property string selectedMac: ""
    property string selectedInverter: ""
    property string originalSerial: ""
    property var results: []
    function load() {
        let s = monitor.settings;
        ip.text = s.ip || "";
        serial.text = String(s.serial || "");
        port.value = Number(s.port || 8899);
        interval.value = Number(s.interval_sec || 5);
        profile.currentIndex = 0;
        for (let n = 0; n < monitor.profileOptions.length; ++n)
            if (monitor.profileOptions[n].id === s.profile)
                profile.currentIndex = n;
        modelName.text = s.model_override || "";
        user.text = s.http_user || "admin";
        password.text = s.http_pass || "admin";
        selectedMac = s.logger_mac || "";
        selectedInverter = s.inverter_serial || "";
        originalSerial = serial.text;
        results = [];
        scanMessage = "Scan finds Solarman loggers on the local network.";
    }
    function settings() {
        let s = {
            ip: ip.text.trim(),
            serial: serial.text.trim(),
            port: port.value,
            interval_sec: interval.value,
            profile: profile.currentValue,
            model_override: modelName.text.trim(),
            http_user: user.text,
            http_pass: password.text
        };
        if (serial.text.trim() === originalSerial) {
            s.logger_mac = selectedMac;
            s.inverter_serial = selectedInverter;
        }
        return s;
    }
    function choose(index) {
        if (index < 0 || index >= results.length)
            return;
        let l = results[index];
        ip.text = l.ip;
        serial.text = l.serial;
        originalSerial = l.serial;
        selectedMac = l.mac;
        selectedInverter = l.inverterSerial;
    }
    onClosing: monitor.cancelScan()
    Connections {
        target: monitor
        function onScanChanged() {
            const previousCount = config.results.length;
            config.results = monitor.scanResults;
            if (monitor.scanning && previousCount === 0 && config.results.length === 1)
                config.choose(0);
            if (monitor.scanning)
                config.scanMessage = config.results.length > 0
                    ? config.results.length + " logger" + (config.results.length === 1 ? "" : "s") + " found · ready to connect; checking for others…"
                    : "Finding loggers…";
        }
        function onScanFinished(loggers, elapsed, error) {
            config.results = loggers;
            config.scanMessage = error || (loggers.length + " logger" + (loggers.length === 1 ? "" : "s") + " found in " + elapsed.toFixed(2) + " s");
        }
    }
    component Field: TextField {
        implicitHeight: 40
        font.pixelSize: 13
        color: "#183d37"
        placeholderTextColor: "#778780"
        selectByMouse: true
        leftPadding: 12
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: parent.activeFocus ? "#2eaa83" : "#d9e5dd"
        }
    }
    component LabelText: Text {
        color: "#607a6b"
        font.pixelSize: 13
    }
    component HelpText: Text {
        color: "#778780"
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component NumberField: SpinBox {
        id: spin
        implicitHeight: 40
        implicitWidth: 170
        editable: true
        font.pixelSize: 13
        leftPadding: 34
        rightPadding: 34
        textFromValue: (value, locale) => String(value)
        valueFromText: (text, locale) => Number(text)
        contentItem: TextInput {
            text: spin.textFromValue(spin.value, spin.locale)
            color: "#183d37"
            font: spin.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhDigitsOnly
            selectByMouse: true
        }
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: spin.activeFocus ? "#2eaa83" : "#d9e5dd"
        }
        up.indicator: Rectangle {
            x: spin.width - width
            width: 33
            height: spin.height
            radius: 8
            color: spin.up.pressed ? "#d9eee2" : "#edf4ee"
            Text {
                anchors.centerIn: parent
                text: "+"
                font.pixelSize: 19
                color: "#486c57"
            }
        }
        down.indicator: Rectangle {
            width: 33
            height: spin.height
            radius: 8
            color: spin.down.pressed ? "#d9eee2" : "#edf4ee"
            Text {
                anchors.centerIn: parent
                text: "−"
                font.pixelSize: 19
                color: "#486c57"
            }
        }
    }
    component ChoiceField: ComboBox {
        id: choice
        implicitHeight: 40
        font.pixelSize: 12
        leftPadding: 12
        rightPadding: 32
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: choice.activeFocus ? "#2eaa83" : "#d9e5dd"
        }
        contentItem: Text {
            text: choice.displayText
            font: choice.font
            color: "#183d37"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: choice.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            font.pixelSize: 20
            color: "#486c57"
        }
    }
    component FormButton: Button {
        property bool primary: false
        implicitHeight: 40
        leftPadding: 18
        rightPadding: 18
        contentItem: Text {
            text: parent.text
            color: parent.primary ? "white" : "#486c57"
            font.pixelSize: 12
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: parent.primary ? (parent.hovered ? "#067b60" : "#008d70") : (parent.hovered ? "#e5eee7" : "#edf3ee")
            border.color: parent.primary ? "transparent" : "#d9e5dd"
            opacity: parent.enabled ? 1 : .5
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18
        Text {
            text: "Connection settings"
            font.pixelSize: 24
            font.weight: Font.DemiBold
            color: "#183d37"
        }
        HelpText {
            text: "Find your logger and let Deye select its inverter profile."
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 11
                LabelText {
                    text: "Logger IP address"
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Field {
                        id: ip
                        objectName: "loggerIp"
                        Layout.fillWidth: true
                        placeholderText: "192.168.1.…"
                    }
                    FormButton {
                        objectName: "scanButton"
                        text: monitor.scanning ? "Cancel scan" : "Scan network"
                        onClicked: {
                            if (monitor.scanning)
                                monitor.cancelScan();
                            else {
                                config.scanMessage = "Finding loggers…";
                                monitor.scanSettings(config.settings());
                            }
                        }
                    }
                }
                HelpText {
                    text: config.scanMessage
                }
                ChoiceField {
                    visible: config.results.length > 1
                    Layout.fillWidth: true
                    model: config.results.map(l => l.ip + " · " + l.serial)
                    onActivated: index => config.choose(index)
                }
                LabelText {
                    text: "Logger serial number"
                }
                Field {
                    id: serial
                    Layout.fillWidth: true
                    placeholderText: "Filled by Scan"
                    validator: RegularExpressionValidator {
                        regularExpression: /[0-9]{0,10}/
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    ColumnLayout {
                        Layout.fillWidth: true
                        LabelText {
                            text: "TCP port"
                        }
                        NumberField {
                            id: port
                            Layout.fillWidth: true
                            from: 1
                            to: 65535
                            value: 8899
                            editable: true
                            font.pixelSize: 12
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        LabelText {
                            text: "Refresh interval (seconds)"
                        }
                        NumberField {
                            id: interval
                            Layout.fillWidth: true
                            from: 1
                            to: 300
                            value: 5
                            editable: true
                            font.pixelSize: 12
                        }
                    }
                }
                LabelText {
                    text: "Inverter profile"
                }
                ChoiceField {
                    id: profile
                    Layout.fillWidth: true
                    implicitHeight: 40
                    font.pixelSize: 12
                    textRole: "name"
                    valueRole: "id"
                    model: monitor.profileOptions
                }
                HelpText {
                    text: "Unsupported devices stop with a clear message. After a connection loss, the same logger is found and reconnected automatically."
                }
                LabelText {
                    text: "Display model name (optional)"
                }
                Field {
                    id: modelName
                    Layout.fillWidth: true
                    placeholderText: "Use detected model"
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    ColumnLayout {
                        Layout.fillWidth: true
                        LabelText {
                            text: "Logger web user"
                        }
                        Field {
                            id: user
                            Layout.fillWidth: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        LabelText {
                            text: "Logger web password"
                        }
                        Field {
                            id: password
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            HelpText {
                text: "Local access · read only"
            }
            FormButton {
                objectName: "cancelSettings"
                text: "Cancel"
                onClicked: {
                    monitor.cancelScan();
                    config.hide();
                }
            }
            FormButton {
                objectName: "saveSettings"
                text: "Save & connect"
                primary: true
                enabled: ip.text.trim().length > 0
                onClicked: {
                    monitor.cancelScan();
                    monitor.applySettings(config.settings());
                    config.hide();
                }
            }
        }
    }
}
