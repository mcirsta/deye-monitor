import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card
    property string title
    property string value: "—"
    property string unit: "kW"
    property string note
    property string icon: "sun"
    property color accent: "#e9a23b"
    property color titleColor: "#607772"
    property color valueColor: "#183d37"
    property bool emphasizeTitle: false
    property bool wrapTitle: false
    color: "white"
    radius: 16
    border.color: "#e0e8e4"
    implicitHeight: 144
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 7
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: card.title
                color: card.titleColor
                font.pixelSize: 13
                font.weight: card.emphasizeTitle ? Font.DemiBold : Font.Medium
                wrapMode: card.wrapTitle ? Text.WordWrap : Text.NoWrap
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            Rectangle {
                width: 30
                height: 30
                radius: 9
                color: Qt.rgba(card.accent.r, card.accent.g, card.accent.b, .1)
                Icon {
                    objectName: card.objectName === "batteryPowerCard" ? "batteryPowerIcon" : ""
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    name: card.icon
                    color: card.accent
                }
            }
        }
        RowLayout {
            spacing: 7
            Text {
                text: card.value
                font.pixelSize: 32
                font.weight: Font.DemiBold
                color: card.valueColor
            }
            Text {
                text: card.unit
                font.pixelSize: 14
                color: "#81938c"
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 6
            }
        }
        Text {
            text: card.note
            visible: text.length > 0
            color: "#83918c"
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
