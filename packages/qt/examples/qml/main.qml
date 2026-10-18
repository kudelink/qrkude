// QRKude Qt - QML example
// Copyright 2026 The QRKude Authors
// SPDX-License-Identifier: Apache-2.0
import QtQuick 2.12
import QtQuick.Window 2.12
import QRKude 1.0

Window {
    id: window
    width: 760
    height: 600
    visible: true
    title: "QRKude QML"
    color: "#f4f1ea"

    readonly property var shapes: ["square", "dot", "rounded", "diamond", "star", "fluid",
                                   "liquid", "bevel", "classy", "hbars", "vbars"]
    readonly property var eyes: ["square", "rounded", "circle", "leaf", "drop", "bevel", "diamond", "dots"]
    readonly property var colors: ["#1d3557", "#0b3d91", "#2a9d8f", "#6a11cb", "#000000"]
    property int shapeIndex: 6
    property int eyeIndex: 3
    property int colorIndex: 0
    property bool withLogo: true
    // a small PNG logo as a data: URL (local files and qrc: URLs work too)
    readonly property string logoData: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAA+UlEQVR42u2a0Q3CMAxEY6tLwCYwQJkZBoBN6BjwhYSqUpzYSc6R/Z0m73zXVkmbkvMiy8mep/klGXd83AhGgBS6lhjqBW4lhHqDa4UwEnzJGoQCXuoGo8JL12ZUeCkDI8NLWDg5L0bv/j8m9gC/xzZWhJC7/4tx0kx2uF9NoJbzRe+Ah+5vsY75GHUnwFN81jGKCIWAEBACQkAI8C/A8qyyVX2YI0IwAjzF6JtVtSPT7KSqRMiDC2vG8W5iZBe22Fg6EBF+7PcAkgt7LFx6IQK8KEI9RUjWzoJrdfyS0zSuNXEL+GwHarrR7EO3tZBuvxpoxHjcQFWrN/QhcMAI5meBAAAAAElFTkSuQmCC"

    Row {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        QRKudeItem {
            id: qr
            width: 440
            height: 440 * implicitHeight / implicitWidth
            text: input.text
            // base style; the properties below override parts of it
            styleJson: '{"background": {"radius": 3}, "border": {"width": 0.5, "color": "#1d3557"},'
                       + ' "logo": {"size": 0.22, "shape": "circle", "background": "#ffffff"}}'
            moduleShape: window.shapes[window.shapeIndex]
            eyeFrame: window.eyes[window.eyeIndex]
            eyeBall: "circle"
            color: window.colors[window.colorIndex]
            backgroundColor: "white"
            logoSource: window.withLogo ? window.logoData : ""
            label: "SCAN ME"
        }

        Column {
            spacing: 12
            width: 240

            Rectangle {
                width: parent.width
                height: 36
                radius: 6
                border.color: "#9aa5b1"
                TextInput {
                    id: input
                    anchors.fill: parent
                    anchors.margins: 8
                    text: "https://kudelink.com/qml"
                    font.pixelSize: 15
                    clip: true
                }
            }
            ActionButton {
                text: "Modules: " + qr.moduleShape
                onClicked: window.shapeIndex = (window.shapeIndex + 1) % window.shapes.length
            }
            ActionButton {
                text: "Eyes: " + qr.eyeFrame
                onClicked: window.eyeIndex = (window.eyeIndex + 1) % window.eyes.length
            }
            ActionButton {
                text: "Color: " + qr.color
                onClicked: window.colorIndex = (window.colorIndex + 1) % window.colors.length
            }
            ActionButton {
                text: window.withLogo ? "Remove logo" : "Add logo"
                onClicked: window.withLogo = !window.withLogo
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: "#1d3557"
                text: qr.valid
                      ? "Version " + qr.version + ", logo damage " + qr.logoDamage.toFixed(2)
                        + (qr.lint.length ? "\nWarnings: " + qr.lint.join(", ") : "\nNo warnings")
                      : "Error: " + qr.errorString
            }
        }
    }
}
