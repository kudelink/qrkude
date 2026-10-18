// QRKude Qt - QML example: minimal button (no Qt Quick Controls needed)
// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
import QtQuick 2.12

Rectangle {
    id: button
    property alias text: label.text
    signal clicked()

    width: 220
    height: 36
    radius: 6
    color: area.pressed ? "#1d3557" : (area.containsMouse ? "#264a75" : "#2f5d8f")

    Text {
        id: label
        anchors.centerIn: parent
        color: "white"
        font.pixelSize: 15
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
