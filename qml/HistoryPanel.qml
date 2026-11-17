import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Deye 1.0

Rectangle {
    id: panel
    property var samples: []
    property int minutes: 15
    color: "white"
    radius: 16
    border.color: "#e0e8e4"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 15
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text {
                    text: "Power history"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: "#183d37"
                }
                Text {
                    text: "This session · negative battery power means charging"
                    font.pixelSize: 10
                    color: "#83958d"
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Row {
                spacing: 3
                Repeater {
                    model: [5, 15, 60]
                    delegate: Button {
                        required property int modelData
                        width: 42
                        height: 29
                        text: modelData + "m"
                        onClicked: panel.minutes = modelData
                        contentItem: Text {
                            text: parent.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: panel.minutes === modelData ? "#007b63" : "#83958d"
                            font.pixelSize: 11
                        }
                        background: Rectangle {
                            color: panel.minutes === modelData ? "#eaf5ef" : "transparent"
                            radius: 7
                        }
                    }
                }
            }
        }
        HistoryChart {
            objectName: "powerHistoryChart"
            Layout.fillWidth: true
            Layout.fillHeight: true
            samples: panel.samples
            minutes: panel.minutes
        }
        Row {
            spacing: 22
            Layout.alignment: Qt.AlignHCenter
            Repeater {
                model: [
                    {
                        label: "Solar",
                        c: "#e9a23b"
                    },
                    {
                        label: "Home",
                        c: "#5485c0"
                    },
                    {
                        label: "Battery",
                        c: "#21a482"
                    },
                    {
                        label: "Grid",
                        c: "#9679bc"
                    }
                ]
                delegate: Row {
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        width: 15
                        height: 3
                        radius: 2
                        color: modelData.c
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        color: "#7a8e86"
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
