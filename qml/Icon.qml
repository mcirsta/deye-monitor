import QtQuick

Canvas {
    id: icon
    property string name: "sun"
    property color color: "#173a38"
    implicitWidth: 24
    implicitHeight: 24
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        let c = getContext("2d");
        c.reset();
        c.scale(width / 24, height / 24);
        c.strokeStyle = color;
        c.lineWidth = 1.6;
        c.lineCap = "round";
        c.lineJoin = "round";
        function line(x, y, a, b) {
            c.moveTo(x, y);
            c.lineTo(a, b);
        }
        c.beginPath();
        if (name === "sun") {
            c.arc(12, 12, 4, 0, Math.PI * 2);
            for (let i = 0; i < 8; i++) {
                let a = i * Math.PI / 4;
                line(12 + 7 * Math.cos(a), 12 + 7 * Math.sin(a), 12 + 10 * Math.cos(a), 12 + 10 * Math.sin(a));
            }
        } else if (name === "home") {
            line(3, 11, 12, 3);
            c.lineTo(21, 11);
            line(5, 10, 5, 21);
            c.lineTo(10, 21);
            c.lineTo(10, 15);
            c.lineTo(14, 15);
            c.lineTo(14, 21);
            c.lineTo(19, 21);
            c.lineTo(19, 10);
        } else if (name === "battery") {
            c.roundedRect(3, 6, 17, 12, 2, 2);
            line(22, 10, 22, 14);
            line(7, 12, 16, 12);
            line(11.5, 8.5, 11.5, 15.5);
        } else if (name === "grid") {
            line(6, 21, 11, 3);
            c.lineTo(13, 3);
            c.lineTo(18, 21);
            line(5, 9, 19, 9);
            line(3, 15, 21, 15);
            line(8, 9, 16, 15);
            line(16, 9, 8, 15);
        } else if (name === "inverter") {
            c.roundedRect(5, 2, 14, 20, 3, 3);
            c.rect(8, 6, 8, 4);
            line(8, 15, 16, 15);
            line(8, 18, 13, 18);
        } else if (name === "overview") {
            c.roundedRect(3, 3, 7, 7, 1, 1);
            c.roundedRect(14, 3, 7, 7, 1, 1);
            c.roundedRect(3, 14, 7, 7, 1, 1);
            c.roundedRect(14, 14, 7, 7, 1, 1);
        } else if (name === "chart") {
            line(3, 3, 3, 21);
            c.lineTo(22, 21);
            line(6, 16, 10, 10);
            c.lineTo(14, 13);
            c.lineTo(20, 5);
        } else if (name === "settings") {
            c.arc(12, 12, 4, 0, 2 * Math.PI);
            for (let i = 0; i < 8; i++) {
                let a = i * Math.PI / 4;
                line(12 + 7 * Math.cos(a), 12 + 7 * Math.sin(a), 12 + 9 * Math.cos(a), 12 + 9 * Math.sin(a));
            }
        } else if (name === "refresh") {
            c.arc(12, 12, 8, .2, 5.4);
            line(17, 3, 18, 8);
            c.lineTo(13, 8);
        } else if (name === "pause") {
            line(8, 5, 8, 19);
            line(16, 5, 16, 19);
        } else if (name === "play") {
            line(8, 4, 19, 12);
            c.lineTo(8, 20);
            c.closePath();
        } else if (name === "scan") {
            c.arc(10, 10, 6, 0, Math.PI * 2);
            line(15, 15, 21, 21);
        } else if (name === "arrow") {
            line(5, 12, 19, 12);
            line(14, 7, 19, 12);
            c.lineTo(14, 17);
        } else {
            line(4, 7, 20, 7);
            line(4, 12, 20, 12);
            line(4, 17, 20, 17);
        }
        c.stroke();
    }
}
