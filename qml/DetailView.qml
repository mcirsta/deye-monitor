import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: detail
    property var groups: []
    contentHeight: contents.implicitHeight
    clip: true
    ColumnLayout {
        id: contents
        width: parent.width
        spacing: 12
        GridLayout {
            id: grid
            Layout.fillWidth: true
            columns: width > 1100 ? 3 : 2
            columnSpacing: 18
            rowSpacing: 18
            Repeater {
                model: detail.groups
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    objectName: card.modelData.title === "Battery" ? "batteryCard" :
                                card.modelData.title === "Advanced battery settings" ? "advancedBatterySettingsCard" :
                                card.modelData.title === "AC output settings" ? "acOutputSettingsCard" :
                                card.modelData.title === "Grid voltage limits" ? "gridVoltageLimitsCard" : ""
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: cardContents.implicitHeight + 40
                    color: "white"
                    radius: 16
                    border.color: "#e0e8e4"
                    ColumnLayout {
                        id: cardContents
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 20
                        }
                        spacing: 15
                        Text {
                            text: card.modelData.title
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: "#183d37"
                        }
                        Text {
                            visible: Boolean(card.modelData.note)
                            text: card.modelData.note || ""
                            color: "#7a8e86"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: card.modelData.rows
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.label
                                    color: "#7a8e86"
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: typeof modelData.value === "string" ? modelData.value :
                                          Number(modelData.value).toLocaleString(Qt.locale(), 'f', Number.isInteger(modelData.value) ? 0 : 2) +
                                          (modelData.unit ? " " + modelData.unit : "")
                                    color: "#234339"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Text {
        visible: detail.groups.length === 0
        anchors.centerIn: parent
        text: "Telemetry appears after a complete reading."
        color: "#83958d"
    }
}
