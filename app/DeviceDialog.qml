// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Explicit device picker for the cases where several BLE devices are nearby
// and the automatic selection refuses to guess.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmpBeam

Dialog {
  id: dialog

  required property AppController controller

  property string selectedAddress: ""
  property string selectedName: ""

  readonly property string connectedLabel: controller.deviceName.length > 0
                                            ? controller.deviceName
                                            : controller.deviceAddress

  title: qsTr("Select a meter")
  modal: true
  anchors.centerIn: Overlay.overlay
  width: Math.max(340, Math.min(560, Overlay.overlay ? Overlay.overlay.width - 48 : 560))
  padding: 18

  background: Rectangle {
    color: "#151b23"
    radius: 12
    border.width: 1
    border.color: "#2a3441"
  }

  contentItem: ColumnLayout {
    spacing: 12

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      AppButton {
        text: dialog.controller.scanning ? qsTr("Stop scan")
              : dialog.controller.connected ? qsTr("Disconnect to scan")
              : qsTr("Scan")
        enabled: dialog.controller.scanning || !dialog.controller.connected
        onClicked: {
          if (dialog.controller.scanning)
            dialog.controller.stopScan()
          else
            dialog.controller.startScan()
        }
      }

      Label {
        Layout.fillWidth: true
        text: dialog.controller.status
        color: "#8a94a6"
        font.pixelSize: 12
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
      }
    }

    RowLayout {
      Layout.fillWidth: true
      visible: dialog.controller.connected
      spacing: 10

      Label {
        Layout.fillWidth: true
        text: qsTr("Connected to %1. A connected meter may stop advertising, so it can be missing from a new scan.")
              .arg(dialog.connectedLabel)
        color: "#8a94a6"
        font.pixelSize: 11
        wrapMode: Text.WordWrap
      }

      AppButton {
        text: qsTr("Disconnect")
        onClicked: dialog.controller.disconnectDevice()
      }
    }

    Rectangle {
      Layout.fillWidth: true
      Layout.preferredHeight: 280
      color: "#10151b"
      radius: 8
      border.width: 1
      border.color: "#242d39"
      clip: true

      ListView {
        id: deviceList
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        spacing: 4
        model: dialog.controller.devices

        delegate: ItemDelegate {
          id: deviceDelegate
          width: ListView.view.width
          highlighted: dialog.selectedAddress === modelData.address
          onClicked: {
            dialog.selectedAddress = modelData.address
            dialog.selectedName = modelData.name
          }

          contentItem: ColumnLayout {
            spacing: 2

            Label {
              Layout.fillWidth: true
              text: modelData.name && modelData.name.length > 0
                    ? modelData.name
                    : qsTr("(unnamed)")
              color: deviceDelegate.highlighted ? "#0b1016" : "#e7edf5"
              font.pixelSize: 14
              font.bold: true
              elide: Text.ElideRight
            }

            Label {
              Layout.fillWidth: true
              text: modelData.address + "  ·  " + modelData.rssi + qsTr(" dBm")
              color: deviceDelegate.highlighted ? "#1b2a24" : "#7e8a9c"
              font.pixelSize: 11
              elide: Text.ElideRight
            }
          }

          background: Rectangle {
            radius: 6
            color: deviceDelegate.highlighted ? "#37d0a0"
                   : deviceDelegate.hovered ? "#1c2430" : "transparent"
          }
        }
      }

      Label {
        anchors.centerIn: parent
        visible: deviceList.count === 0
        text: dialog.controller.scanning ? qsTr("Scanning...")
              : dialog.controller.connected ? qsTr("No other devices found")
              : qsTr("No devices yet")
        color: "#5e6a7c"
        font.pixelSize: 13
      }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      Label {
        Layout.fillWidth: true
        text: qsTr("The scan shows every BLE device; choose the meter from the list.")
        color: "#5e6a7c"
        font.pixelSize: 11
        wrapMode: Text.WordWrap
      }

      AppButton {
        text: qsTr("Connect")
        primary: true
        enabled: dialog.selectedAddress.length > 0
        onClicked: {
          dialog.controller.connectToDevice(dialog.selectedAddress, dialog.selectedName)
          dialog.close()
        }
      }
    }
  }

  onOpened: {
    selectedAddress = ""
    selectedName = ""
    if (!dialog.controller.connected && dialog.controller.devices.length === 0)
      dialog.controller.startScan()
  }

  onClosed: dialog.controller.stopScan()
}
