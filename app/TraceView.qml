// Copyright (C) 2026 Christian Mayer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Signal trace: the curve of the most recent proven readings. This is not a
// decoded oscilloscope; the meter's scope mode is not implemented, so only
// measurements the decoder can prove are plotted.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AmpBeam

Rectangle {
  id: view

  required property AppController controller

  color: "#10151b"
  radius: 8
  border.width: 1
  border.color: "#1f2833"
  clip: true

  property var samples: []

  function refresh() {
    samples = controller.traceSamples
    plot.requestPaint()
  }

  Component.onCompleted: refresh()

  Connections {
    target: view.controller

    function onTraceChanged() {
      view.refresh()
    }
  }

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: 12
    spacing: 6

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      Label {
        id: title
        text: qsTr("SIGNAL TRACE")
        color: "#5e6a7c"
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1.0
        ToolTip.visible: titleHover.hovered
        ToolTip.text: qsTr("Curve of the last readings. Oscilloscope mode is not decoded, so this is not a real scope capture.")

        HoverHandler {
          id: titleHover
        }
      }

      Label {
        visible: view.controller.tracePaused
        text: qsTr("PAUSED")
        color: "#e3b341"
        font.pixelSize: 10
        font.bold: true
        font.letterSpacing: 1.0
      }

      // Clock time of the newest reading; the canvas carries the relative
      // labels of the fixed timebase.
      Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
        text: view.controller.traceNewestText.length > 0
              ? qsTr("last %1").arg(view.controller.traceNewestText)
              : ""
        color: "#5e6a7c"
        font.pixelSize: 11
        elide: Text.ElideLeft
      }

      AppButton {
        text: view.controller.tracePaused ? qsTr("Continue") : qsTr("Pause")
        primary: view.controller.tracePaused
        implicitHeight: 26
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 11
        ToolTip.visible: hovered
        ToolTip.text: view.controller.tracePaused
                        ? qsTr("Follow new readings again")
                        : qsTr("Freeze the curve to inspect it")
        onClicked: view.controller.tracePaused = !view.controller.tracePaused
      }

      AppButton {
        text: qsTr("Clear")
        implicitHeight: 26
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 11
        enabled: view.controller.traceCount > 0
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Empty the trace window")
        onClicked: view.controller.clearTrace()
      }
    }

    Canvas {
      id: plot
      Layout.fillWidth: true
      Layout.fillHeight: true

      onWidthChanged: requestPaint()
      onHeightChanged: requestPaint()

      onPaint: {
        const ctx = getContext("2d")
        ctx.reset()

        const canvasWidth = plot.width
        const canvasHeight = plot.height
        if (canvasWidth <= 0 || canvasHeight <= 0)
          return

        // Room for the maximum labels above the frame, and for the minimum and
        // time labels below it.
        const left = 6
        const right = canvasWidth - 6
        const top = 20
        const bottom = canvasHeight - 30

        function plotX(value) {
          return left + (right - left) * value
        }
        function plotY(value) {
          return bottom - (bottom - top) * value
        }
        function drawValueLabel(context, value, suffix, x, y) {
          if (value.length === 0)
            return
          context.fillText(suffix.length > 0 ? value + " " + suffix : value,
                           x, y)
        }
        function drawChannel(context, series, validKey, yKey, color) {
          context.strokeStyle = color
          context.beginPath()
          let drawing = false
          let lastValid = -1
          for (let index = 0; index < series.length; ++index) {
            const point = series[index]
            if (!point[validKey]) {
              drawing = false
              continue
            }
            const x = plotX(point.x)
            const y = plotY(point[yKey])
            if (drawing) {
              context.lineTo(x, y)
            } else {
              context.moveTo(x, y)
              drawing = true
            }
            lastValid = index
          }
          context.stroke()
          if (lastValid >= 0) {
            context.fillStyle = color
            context.beginPath()
            context.arc(plotX(series[lastValid].x), plotY(series[lastValid][yKey]),
                        3, 0, 2 * Math.PI)
            context.fill()
          }
        }

        // Fixed grid; the vertical lines are the time divisions.
        ctx.lineWidth = 1
        ctx.strokeStyle = "#1c242e"
        for (let row = 0; row <= 4; ++row) {
          const y = top + (bottom - top) * row / 4
          ctx.beginPath()
          ctx.moveTo(left, y)
          ctx.lineTo(right, y)
          ctx.stroke()
        }
        for (let column = 0; column <= 6; ++column) {
          const x = left + (right - left) * column / 6
          ctx.beginPath()
          ctx.moveTo(x, top)
          ctx.lineTo(x, bottom)
          ctx.stroke()
        }

        const points = view.samples
        if (points.length === 0) {
          ctx.fillStyle = "#4b5566"
          ctx.font = "12px monospace"
          ctx.textAlign = "center"
          ctx.fillText(qsTr("Waiting for readings"), canvasWidth / 2,
                       canvasHeight / 2)
        } else {
          // A gap (overload or a frame without a numeric value) breaks the
          // line; two unrelated functions are never joined because the history
          // starts a new series when the mode or unit changes. Clipping keeps
          // a value zoom inside the frame.
          ctx.save()
          ctx.beginPath()
          ctx.rect(left, top, right - left, bottom - top)
          ctx.clip()
          ctx.lineWidth = 2
          ctx.lineJoin = "round"
          ctx.lineCap = "round"
          drawChannel(ctx, points, "valid", "y", "#37d0a0")
          if (view.controller.traceHasSecondary)
            drawChannel(ctx, points, "secondaryValid", "secondaryY", "#5aa9e6")
          ctx.restore()
        }

        // Relative time labels of the fixed timebase. The right edge is the
        // newest reading ("now"); older readings scroll left as time passes.
        const spanMs = view.controller.traceSpanMs
        function spanText(milliseconds) {
          return milliseconds % 1000 === 0
                 ? (milliseconds / 1000) + " s"
                 : (milliseconds / 1000).toFixed(1) + " s"
        }
        ctx.font = "10px monospace"
        ctx.fillStyle = "#5e6a7c"
        ctx.textAlign = "left"
        ctx.fillText("-" + spanText(spanMs), left, canvasHeight - 6)
        ctx.textAlign = "center"
        ctx.fillText("-" + spanText(spanMs / 2), (left + right) / 2,
                     canvasHeight - 6)
        ctx.textAlign = "right"
        ctx.fillText(qsTr("now"), right, canvasHeight - 6)

        // Value range labels, one colour per channel.
        const unit = view.controller.unitText
        const secondaryUnit = view.controller.traceSecondaryUnitText
        ctx.textAlign = "left"
        ctx.fillStyle = "#37d0a0"
        drawValueLabel(ctx, view.controller.traceMaxText, unit, left + 4, top - 5)
        drawValueLabel(ctx, view.controller.traceMinText, unit, left + 4,
                       bottom + 12)
        if (view.controller.traceHasSecondary) {
          ctx.fillStyle = "#5aa9e6"
          ctx.textAlign = "right"
          drawValueLabel(ctx, view.controller.traceSecondaryMaxText,
                         secondaryUnit, right - 4, top - 5)
          drawValueLabel(ctx, view.controller.traceSecondaryMinText,
                         secondaryUnit, right - 4, bottom + 12)
        }
      }
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 4

      Label {
        text: qsTr("TIME")
        color: "#5e6a7c"
        font.pixelSize: 9
        font.bold: true
        font.letterSpacing: 0.8
      }

      AppButton {
        text: qsTr("−")
        implicitHeight: 26
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 13
        enabled: view.controller.traceCanZoomTimeOut
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Longer timebase")
        onClicked: view.controller.zoomTraceTimeOut()
      }

      Label {
        text: view.controller.traceSpanText
        color: "#93a0b3"
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        Layout.minimumWidth: 40
      }

      AppButton {
        text: qsTr("+")
        implicitHeight: 26
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 13
        enabled: view.controller.traceCanZoomTimeIn
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Shorter timebase")
        onClicked: view.controller.zoomTraceTimeIn()
      }

      Item { Layout.preferredWidth: 10 }

      Label {
        text: qsTr("VALUE")
        color: "#5e6a7c"
        font.pixelSize: 9
        font.bold: true
        font.letterSpacing: 0.8
      }

      AppButton {
        text: qsTr("−")
        implicitHeight: 26
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 13
        enabled: view.controller.traceCanZoomValueOut
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Smaller value scale")
        onClicked: view.controller.zoomTraceValueOut()
      }

      Label {
        text: view.controller.traceValueZoomText
        color: "#93a0b3"
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        Layout.minimumWidth: 32
      }

      AppButton {
        text: qsTr("+")
        implicitHeight: 26
        leftPadding: 10
        rightPadding: 10
        font.pixelSize: 13
        enabled: view.controller.traceCanZoomValueIn
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Larger value scale")
        onClicked: view.controller.zoomTraceValueIn()
      }

      Item { Layout.fillWidth: true }

      Label {
        visible: view.width >= 560
        text: qsTr("%1 readings").arg(view.controller.traceCount)
        color: "#5e6a7c"
        font.pixelSize: 11
        elide: Text.ElideLeft
        Layout.maximumWidth: 110
      }
    }
  }
}
