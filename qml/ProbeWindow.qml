import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: probeWindow
    objectName: "diagnosticProbeWindow"
    width: 660
    height: 760
    minimumWidth: 550
    minimumHeight: 600
    title: "Deye · Probe report"
    color: "#f2f6f3"
    visible: false
    modality: Qt.WindowModal
    font.family: "Inter"
    readonly property var controller: monitor.probe
    property string exportMessage: ""
    property bool exportError: false

    function open() {
        if (!monitor.probeOffered)
            return;
        exactModel.text = controller.exactModel || "";
        notes.text = controller.notes || "";
        includeIdentifiers.checked = false;
        exportMessage = "";
        show();
        raise();
        requestActivate();
    }
    onClosing: monitor.cancelProbe()

    Connections {
        target: monitor
        function onChanged() {
            if (probeWindow.visible && (monitor.connected || monitor.demo) && !monitor.probeOffered)
                probeWindow.close();
        }
        function onSettingsChanged() {
            if (probeWindow.visible && !monitor.probeOffered)
                probeWindow.close();
        }
    }

    Connections {
        target: probeWindow.controller
        function onExportFinished(path) {
            probeWindow.exportError = false;
            probeWindow.exportMessage = "Saved report to " + path;
        }
        function onExportFailed(message) {
            probeWindow.exportError = true;
            probeWindow.exportMessage = message;
        }
    }

    FileDialog {
        id: saveDialog
        title: "Save probe report"
        fileMode: FileDialog.SaveFile
        nameFilters: ["JSON reports (*.json)"]
        defaultSuffix: "json"
        onAccepted: {
            probeWindow.controller.setDetails(exactModel.text.trim(), notes.text.trim());
            probeWindow.controller.save(selectedFile, includeIdentifiers.checked);
        }
    }

    component LabelText: Text {
        color: "#607a6b"
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component HelpText: LabelText {
        color: "#778780"
    }
    component ProbeButton: Button {
        id: probeButton
        property bool primary: false
        implicitHeight: 40
        leftPadding: 18
        rightPadding: 18
        contentItem: Text {
            text: probeButton.text
            color: probeButton.primary ? "white" : "#486c57"
            font.pixelSize: 12
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: probeButton.primary ? (probeButton.hovered ? "#067b60" : "#008d70") : (probeButton.hovered ? "#e5eee7" : "#edf3ee")
            border.color: probeButton.primary ? "transparent" : "#d9e5dd"
            opacity: probeButton.enabled ? 1 : .5
        }
    }
    component ProbeCheckBox: CheckBox {
        id: probeCheckBox
        Layout.fillWidth: true
        font.pixelSize: 13
        palette.highlight: "#008d70"
        contentItem: Text {
            text: probeCheckBox.text
            font: probeCheckBox.font
            color: probeCheckBox.enabled ? "#183d37" : "#778780"
            verticalAlignment: Text.AlignVCenter
            leftPadding: probeCheckBox.indicator.width + probeCheckBox.spacing
            wrapMode: Text.WordWrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16
        Text {
            text: "Create a probe report"
            font.pixelSize: 24
            font.weight: Font.DemiBold
            color: "#183d37"
        }
        HelpText {
            text: "Save information about the startup problem to help add or fix support for your inverter or logger. You can also collect more data using the saved connection."
        }
        LabelText {
            objectName: "probeDemoNotice"
            visible: monitor.demo
            text: "Configure a real logger before probing."
            color: "#b45e30"
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 9
                LabelText {
                    text: "Exact inverter model (required)"
                }
                TextField {
                    id: exactModel
                    objectName: "probeExactModel"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    enabled: !probeWindow.controller.running
                    font.pixelSize: 13
                    color: "#183d37"
                    placeholderTextColor: "#778780"
                    selectByMouse: true
                    leftPadding: 12
                    maximumLength: 160
                    placeholderText: "e.g. SUN-10K-SG02LP1-EU-AM3"
                    background: Rectangle {
                        radius: 8
                        color: "white"
                        border.color: parent.activeFocus ? "#2eaa83" : "#d9e5dd"
                    }
                }
                HelpText {
                    text: "Copy the full model from the inverter label."
                }
                LabelText {
                    text: "Reference readings and conditions (optional)"
                }
                TextArea {
                    id: notes
                    objectName: "probeNotes"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    enabled: !probeWindow.controller.running
                    font.pixelSize: 13
                    color: "#183d37"
                    placeholderTextColor: "#778780"
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    padding: 12
                    placeholderText: "Values shown on the inverter LCD at the same time, battery charging or discharging, and current load conditions."
                    background: Rectangle {
                        radius: 8
                        color: "white"
                        border.color: parent.activeFocus ? "#2eaa83" : "#d9e5dd"
                    }
                }
                ProbeCheckBox {
                    id: extended
                    objectName: "probeExtendedRanges"
                    text: "Read known-family ranges for an unsupported model"
                    checked: false
                    enabled: !probeWindow.controller.running
                }
                HelpText {
                    text: "This uses bounded, read-only ranges from known inverter families. It does not sweep registers."
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#dfe8e2"
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                }
                LabelText {
                    objectName: "probeStatus"
                    text: probeWindow.controller.status || "Enter the exact model, then start the probe."
                    color: "#486c57"
                }
                ProgressBar {
                    objectName: "probeProgress"
                    Layout.fillWidth: true
                    visible: probeWindow.controller.running
                    from: 0
                    to: Math.max(1, probeWindow.controller.total)
                    value: probeWindow.controller.completed
                    indeterminate: probeWindow.controller.total <= 0
                    palette.highlight: "#008d70"
                }
                HelpText {
                    visible: probeWindow.controller.running && probeWindow.controller.total > 0
                    text: probeWindow.controller.completed + " / " + probeWindow.controller.total + " reads"
                }
                HelpText {
                    text: "A partial report is useful even if the inverter is unsupported or some reads fail."
                }
                LabelText {
                    objectName: "probeExistingEvidenceNotice"
                    visible: probeWindow.controller.ready && !probeWindow.controller.running
                    text: "Existing evidence can be saved without starting another probe."
                    color: "#486c57"
                }
                ProbeCheckBox {
                    id: includeIdentifiers
                    objectName: "probeIncludeIdentifiers"
                    text: "Include device serial numbers and network addresses"
                    checked: false
                    enabled: !probeWindow.controller.running
                }
                HelpText {
                    text: "Reports omit those identifiers by default. Logger web credentials are never included. Save the JSON file to share it yourself; nothing is uploaded."
                }
                ProbeButton {
                    objectName: "probeExportButton"
                    text: "Save report…"
                    enabled: probeWindow.controller.ready && !probeWindow.controller.running && exactModel.text.trim().length > 0
                    onClicked: saveDialog.open()
                }
                LabelText {
                    objectName: "probeExportMessage"
                    visible: probeWindow.exportMessage.length > 0
                    text: probeWindow.exportMessage
                    color: probeWindow.exportError ? "#b45e30" : "#168466"
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ProbeButton {
                text: "Close"
                onClicked: probeWindow.close()
            }
            Item {
                Layout.fillWidth: true
            }
            ProbeButton {
                objectName: "probeCancelButton"
                text: "Cancel probe"
                visible: probeWindow.controller.running
                onClicked: monitor.cancelProbe()
            }
            ProbeButton {
                objectName: "probeStartButton"
                text: probeWindow.controller.ready ? "Collect more data" : "Start probe"
                primary: true
                enabled: monitor.probeOffered && !probeWindow.controller.running && exactModel.text.trim().length > 0
                onClicked: {
                    probeWindow.exportMessage = "";
                    monitor.startProbe(exactModel.text.trim(), notes.text.trim(), extended.checked);
                }
            }
        }
    }
}
