// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Qt Quick user interface for the AmpBeam desktop and Android application.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmpBeam

ApplicationWindow {
  id: root

  width: 520
  height: 780
  minimumWidth: 420
  minimumHeight: 600
  visible: true
  title: qsTr("AmpBeam")

  AppController {
    id: controller
  }

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: 12
    spacing: 10

    RowLayout {
      Layout.fillWidth: true

      Button {
        text: controller.scanning ? qsTr("Stop") : qsTr("Scan")
        onClicked: controller.scanning ? controller.stopScan() : controller.startScan()
      }

      Label {
        Layout.fillWidth: true
        text: controller.status
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
      }
    }

    Rectangle {
      Layout.fillWidth: true
      Layout.preferredHeight: 180
      radius: 6
      border.width: 1
      border.color: "#888888"
      clip: true

      ListView {
        id: deviceList
        anchors.fill: parent
        anchors.margins: 2
        clip: true
        model: controller.devices

        delegate: ItemDelegate {
          width: ListView.view.width
          text: (modelData.name && modelData.name.length > 0
                 ? modelData.name
                 : qsTr("(unnamed)"))
                + "  ·  " + modelData.address
                + "  ·  " + modelData.rssi + " dBm"
          onClicked: controller.connectToDevice(modelData.address, modelData.name)
        }
      }

      Label {
        anchors.centerIn: parent
        visible: deviceList.count === 0
        text: qsTr("No devices yet — press Scan")
        color: "#888888"
      }
    }

    Label {
      Layout.fillWidth: true
      horizontalAlignment: Text.AlignRight
      font.pixelSize: 42
      font.bold: true
      text: controller.valueText
    }

    Label {
      Layout.fillWidth: true
      horizontalAlignment: Text.AlignRight
      font.family: "monospace"
      font.pixelSize: 12
      elide: Text.ElideRight
      text: controller.rawHex.length > 0
            ? controller.rawHex
            : qsTr("decoder pending — raw frames below")
    }

    RowLayout {
      Button {
        text: qsTr("Disconnect")
        enabled: controller.connected
        onClicked: controller.disconnectDevice()
      }
      Button {
        text: qsTr("Clear log")
        onClicked: controller.clearLog()
      }
    }

    ScrollView {
      Layout.fillWidth: true
      Layout.fillHeight: true
      clip: true

      TextArea {
        readOnly: true
        font.family: "monospace"
        font.pixelSize: 11
        wrapMode: TextArea.WrapAnywhere
        text: controller.logText
      }
    }
  }
}
