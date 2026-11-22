// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared button styling for the desktop application.
import QtQuick
import QtQuick.Controls

Button {
  id: control

  property bool primary: false
  property bool danger: false

  implicitHeight: 36
  leftPadding: 16
  rightPadding: 16
  font.pixelSize: 13
  font.weight: Font.DemiBold
  hoverEnabled: true

  contentItem: Text {
    text: control.text
    font: control.font
    color: {
      if (!control.enabled)
        return "#5e6a7c"
      if (control.primary || control.danger)
        return "#0b1016"
      return "#dfe7f1"
    }
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
  }

  background: Rectangle {
    radius: 8
    color: {
      if (!control.enabled)
        return "#1b212a"
      if (control.danger)
        return control.down ? "#d95c4b" : control.hovered ? "#f47664" : "#ef6a58"
      if (control.primary)
        return control.down ? "#22a87f" : control.hovered ? "#3ad2a4" : "#2bbf92"
      return control.down ? "#2c3542" : control.hovered ? "#283140" : "#222a36"
    }
    border.width: control.primary || control.danger ? 0 : 1
    border.color: "#354052"
    Behavior on color { ColorAnimation { duration: 90 } }
  }
}
