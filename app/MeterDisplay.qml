// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Large meter face: mode, value, unit, verified indicators and the secondary
// reading (MAX in the MinMax view, the stored reference in REL mode).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmpBeam

Rectangle {
  id: card

  required property AppController controller

  radius: 16
  color: "#151b23"
  border.width: 1
  border.color: "#26303d"

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: 22
    spacing: 8

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      Label {
        text: card.controller.modeText.length > 0 ? card.controller.modeText : qsTr("NO SIGNAL")
        color: card.controller.modeText.length > 0 ? "#37d0a0" : "#4b5566"
        font.pixelSize: 15
        font.bold: true
        font.letterSpacing: 1.2
      }

      Item { Layout.fillWidth: true }

      RowLayout {
        spacing: 6
        Repeater {
          model: card.controller.indicators
          IndicatorChip {
            label: modelData.label
            tone: modelData.tone
          }
        }
      }
    }

    Item { Layout.fillHeight: true }

    RowLayout {
      Layout.fillWidth: true
      spacing: 10

      Label {
        Layout.fillWidth: true
        text: card.controller.numberText
        color: card.controller.hasReading ? "#f4f8fd" : "#4b5566"
        font.family: "monospace"
        font.pixelSize: 72
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        maximumLineCount: 1
      }

      Label {
        text: card.controller.unitText
        color: "#8a94a6"
        font.pixelSize: 30
        font.weight: Font.Medium
        Layout.alignment: Qt.AlignBottom
        Layout.bottomMargin: 12
      }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 12

      Label {
        text: card.controller.secondaryText
        visible: text.length > 0
        color: "#8a94a6"
        font.family: "monospace"
        font.pixelSize: 17
      }

      Item { Layout.fillWidth: true }

      Label {
        text: card.controller.lastUpdateText
        visible: text.length > 0
        color: "#5e6a7c"
        font.family: "monospace"
        font.pixelSize: 11
      }
    }
  }
}
