// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Qt Quick user interface for the AmpBeam desktop and Android application.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import AmpBeam

ApplicationWindow {
  id: root

  width: 980
  height: 780
  minimumWidth: 380
  minimumHeight: 620
  visible: true
  title: qsTr("AmpBeam")
  color: "#0e1218"

  palette.window: "#0e1218"
  palette.windowText: "#e7edf5"
  palette.base: "#10151b"
  palette.alternateBase: "#151b23"
  palette.text: "#e7edf5"
  palette.button: "#222a36"
  palette.buttonText: "#dfe7f1"
  palette.highlight: "#2bbf92"
  palette.highlightedText: "#0b1016"
  palette.toolTipBase: "#1b222c"
  palette.toolTipText: "#e7edf5"
  palette.mid: "#2a3441"
  palette.dark: "#0b0f14"
  palette.light: "#354052"

  readonly property color statusColor: controller.streaming ? "#2bbf92"
                                     : controller.connected ? "#e3b341"
                                     : controller.autoConnecting ? "#5aa9e6"
                                     : "#4b5566"

  readonly property string deviceLabel: {
    if (controller.deviceAddress.length === 0)
      return qsTr("BLE multimeter")
    if (controller.deviceName.length === 0)
      return controller.deviceAddress
    return controller.deviceName + " · " + controller.deviceAddress
  }

  property string pendingFileAction: ""

  function deliverSelectedFile(path) {
    if (pendingFileAction === "csv")
      controller.exportCsv(path)
    else if (pendingFileAction === "capture")
      controller.startCapture(path)
    pendingFileAction = ""
  }

  AppController {
    id: controller
  }

  PortalFileDialog {
    id: portalFiles

    onAccepted: function(filePath) {
      root.deliverSelectedFile(filePath)
    }

    onRejected: root.pendingFileAction = ""

    onFailed: function(message) {
      console.warn("desktop file chooser failed:", message)
      if (root.pendingFileAction === "csv")
        csvDialog.open()
      else if (root.pendingFileAction === "capture")
        captureDialog.open()
    }
  }

  // Match the CLI's default action: discover the meter immediately and keep
  // reconnecting. Stop is available in the header while this is running.
  Component.onCompleted: controller.connectAutomatically()

  header: Rectangle {
    color: "#11161d"
    implicitHeight: 66

    Rectangle {
      anchors.bottom: parent.bottom
      width: parent.width
      height: 1
      color: "#1c242e"
    }

    RowLayout {
      anchors.fill: parent
      anchors.leftMargin: 20
      anchors.rightMargin: 20
      spacing: 12

      Rectangle {
        width: 10
        height: 10
        radius: 5
        color: root.statusColor
        Layout.alignment: Qt.AlignVCenter
      }

      ColumnLayout {
        spacing: 1
        Layout.maximumWidth: Math.max(120, Math.min(320, root.width - 300))

        Label {
          Layout.fillWidth: true
          text: qsTr("AmpBeam")
          color: "#f2f6fb"
          font.pixelSize: 18
          font.bold: true
          elide: Text.ElideRight
        }

        Label {
          Layout.fillWidth: true
          text: root.deviceLabel
          color: "#6f7b8d"
          font.pixelSize: 11
          elide: Text.ElideRight
        }
      }

      Item { Layout.fillWidth: true }

      BusyIndicator {
        running: controller.scanning || controller.autoConnecting
        visible: running
        implicitWidth: 26
        implicitHeight: 26
      }

      Label {
        Layout.maximumWidth: 260
        visible: root.width >= 760
        text: controller.status
        color: "#93a0b3"
        font.pixelSize: 12
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
      }

      AppButton {
        text: qsTr("Devices...")
        onClicked: deviceDialog.open()
      }

      AppButton {
        text: controller.connected ? qsTr("Disconnect")
              : controller.autoConnecting ? qsTr("Stop")
              : qsTr("Connect")
        primary: !controller.connected && !controller.autoConnecting
        danger: controller.connected
        onClicked: {
          if (controller.connected)
            controller.disconnectDevice()
          else if (controller.autoConnecting)
            controller.stopAutoConnect()
          else
            controller.connectAutomatically()
        }
      }
    }
  }

  ColumnLayout {
    anchors.fill: parent
    anchors.leftMargin: 20
    anchors.rightMargin: 20
    anchors.topMargin: 16
    anchors.bottomMargin: 16
    spacing: 12

    MeterDisplay {
      Layout.fillWidth: true
      Layout.preferredHeight: 250
      Layout.minimumHeight: 220
      controller: controller
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      Label {
        text: qsTr("FRONT PANEL")
        color: "#5e6a7c"
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1.0
      }

      AppButton {
        text: qsTr("Select")
        enabled: controller.streaming
        onClicked: controller.pressKey(1)
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Key 1 - next function in the current group")
      }

      AppButton {
        text: qsTr("Range")
        enabled: controller.streaming
        onClicked: controller.pressKey(2)
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Key 2 - next range; leaves automatic ranging")
      }

      AppButton {
        text: qsTr("Hold")
        enabled: controller.streaming
        onClicked: controller.pressKey(3)
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Key 3 - toggle the HOLD indicator")
      }

      Item { Layout.fillWidth: true }

      Label {
        text: controller.streaming ? qsTr("One keystroke per press")
                                  : qsTr("Connect to use the keys")
        color: "#4b5566"
        font.pixelSize: 11
      }
    }

    TabBar {
      id: tabs
      Layout.fillWidth: true

      background: Rectangle { color: "transparent" }

      TabButton {
        text: qsTr("Trace")
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Curve of the last readings")
      }
      TabButton { text: qsTr("Device") }
      TabButton {
        text: qsTr("Raw")
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Extended information: every received frame as hex")
      }
    }

    StackLayout {
      Layout.fillWidth: true
      Layout.fillHeight: true
      currentIndex: tabs.currentIndex

      TraceView {
        controller: controller
      }

      ColumnLayout {
        spacing: 14

        GridLayout {
          columns: 2
          columnSpacing: 28
          rowSpacing: 10

          Label {
            text: qsTr("Series")
            color: "#5e6a7c"
            font.pixelSize: 11
            font.bold: true
          }
          Label {
            text: controller.deviceSeriesText
            color: "#e7edf5"
            font.family: "monospace"
          }

          Label {
            text: qsTr("Firmware")
            color: "#5e6a7c"
            font.pixelSize: 11
            font.bold: true
          }
          Label {
            text: controller.deviceVersionText
            color: "#e7edf5"
            font.family: "monospace"
          }

          Label {
            text: qsTr("Device")
            color: "#5e6a7c"
            font.pixelSize: 11
            font.bold: true
          }
          Label {
            text: root.deviceLabel
            color: "#e7edf5"
            elide: Text.ElideRight
            Layout.fillWidth: true
          }

          Label {
            text: qsTr("Readings")
            color: "#5e6a7c"
            font.pixelSize: 11
            font.bold: true
          }
          Label {
            text: controller.readingCount
            color: "#e7edf5"
            font.family: "monospace"
          }
        }

        Rectangle {
          Layout.fillWidth: true
          height: 1
          color: "#1f2833"
        }

        Label {
          Layout.fillWidth: true
          wrapMode: Text.WordWrap
          color: "#7e8a9c"
          font.pixelSize: 12
          text: qsTr("The meter selects its function locally. The verified remote keys are Select, Range and Hold; the GUI never guesses a command the protocol does not prove.")
        }

        Item { Layout.fillHeight: true }
      }

      ColumnLayout {
        spacing: 8

        RowLayout {
          Layout.fillWidth: true
          spacing: 8

          Label {
            text: qsTr("RAW FRAMES")
            color: "#5e6a7c"
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1.0
          }

          Item { Layout.fillWidth: true }

          CheckBox {
            id: followTail
            text: qsTr("Follow")
            checked: true
            font.pixelSize: 11
          }

          AppButton {
            text: qsTr("Clear")
            onClicked: controller.clearLog()
          }
        }

        Rectangle {
          Layout.fillWidth: true
          Layout.fillHeight: true
          color: "#10151b"
          radius: 8
          border.width: 1
          border.color: "#1f2833"
          clip: true

          ScrollView {
            id: logScroll
            anchors.fill: parent
            anchors.margins: 8
            clip: true

            TextArea {
              id: logArea
              width: logScroll.availableWidth
              height: Math.max(contentHeight + topPadding + bottomPadding,
                               logScroll.availableHeight)
              text: controller.logText
              readOnly: true
              selectByMouse: true
              wrapMode: TextArea.WrapAnywhere
              color: "#aab6c6"
              font.family: "monospace"
              font.pixelSize: 11
              background: null

              onTextChanged: {
                if (followTail.checked)
                  Qt.callLater(function() {
                    if (!logScroll.contentItem)
                      return
                    logScroll.contentItem.contentY =
                        Math.max(0, logScroll.contentItem.contentHeight
                                    - logScroll.availableHeight)
                  })
              }
            }
          }
        }

        Label {
          Layout.fillWidth: true
          text: controller.detailText.length > 0
                ? controller.detailText
                : qsTr("Frames appear here as the meter sends them.")
          color: "#5e6a7c"
          font.family: "monospace"
          font.pixelSize: 10
          elide: Text.ElideRight
        }

        Label {
          Layout.fillWidth: true
          wrapMode: Text.WordWrap
          color: "#5e6a7c"
          font.pixelSize: 11
          text: qsTr("Raw rows use the capture format: epoch milliseconds, frame length, hex bytes and the decoded fields. Record a capture to replay or import it later.")
        }
      }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      AppButton {
        text: qsTr("Export CSV...")
        enabled: controller.readingCount > 0
        onClicked: {
          root.pendingFileAction = "csv"
          if (portalFiles.isAvailable())
            portalFiles.saveFile(qsTr("Export readings as CSV"), "readings.csv")
          else
            csvDialog.open()
        }
      }

      AppButton {
        text: controller.capturing ? qsTr("Stop capture") : qsTr("Record capture...")
        primary: controller.capturing
        danger: controller.capturing
        onClicked: {
          if (controller.capturing) {
            controller.stopCapture()
            return
          }
          root.pendingFileAction = "capture"
          if (portalFiles.isAvailable())
            portalFiles.saveFile(qsTr("Record capture"), "capture.log")
          else
            captureDialog.open()
        }
      }

      Label {
        Layout.fillWidth: true
        text: controller.capturing
              ? qsTr("Recording %1 frame(s) to %2")
                  .arg(controller.capturedFrames)
                  .arg(controller.capturePath)
              : qsTr("%1 reading(s) this session").arg(controller.readingCount)
        color: "#5e6a7c"
        font.pixelSize: 11
        elide: Text.ElideMiddle
      }
    }
  }

  DeviceDialog {
    id: deviceDialog
    controller: controller
  }

  FileDialog {
    id: csvDialog
    title: qsTr("Export readings as CSV")
    fileMode: FileDialog.SaveFile
    nameFilters: [qsTr("CSV files (*.csv)"), qsTr("All files (*)")]
    defaultSuffix: "csv"
    onAccepted: root.deliverSelectedFile(selectedFile.toString())
    onRejected: root.pendingFileAction = ""
  }

  FileDialog {
    id: captureDialog
    title: qsTr("Record capture")
    fileMode: FileDialog.SaveFile
    nameFilters: [qsTr("Capture logs (*.log)"), qsTr("Text files (*.txt)"),
                  qsTr("All files (*)")]
    defaultSuffix: "log"
    onAccepted: root.deliverSelectedFile(selectedFile.toString())
    onRejected: root.pendingFileAction = ""
  }

  Connections {
    target: controller

    function onDeviceSelectionRequiredChanged() {
      if (controller.deviceSelectionRequired)
        deviceDialog.open()
    }
  }
}
