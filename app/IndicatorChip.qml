// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One verified indicator (HOLD, REL, AUTO, MANUAL, MINMAX, OVERLOAD).
import QtQuick
import QtQuick.Controls

Rectangle {
  id: chip

  property string label: ""
  property string tone: "neutral"

  readonly property color toneColor: {
    switch (tone) {
    case "warn":
      return "#ff7866"
    case "hold":
      return "#ffc857"
    case "accent":
      return "#37d0a0"
    case "muted":
      return "#7e8a9c"
    default:
      return "#8a94a6"
    }
  }

  implicitWidth: chipLabel.implicitWidth + 16
  implicitHeight: 22
  radius: 11
  color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.14)
  border.width: 1
  border.color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.4)

  Label {
    id: chipLabel
    anchors.centerIn: parent
    text: chip.label
    color: chip.toneColor
    font.pixelSize: 10
    font.bold: true
    font.letterSpacing: 0.8
  }
}
