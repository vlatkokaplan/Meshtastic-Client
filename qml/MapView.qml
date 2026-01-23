import QtQuick
import QtLocation
import QtPositioning

Item {
    id: root

    signal nodeClicked(var nodeNum)
    signal mapReady()

    property var nodeModel: []

    function centerOn(lat, lon) {
        map.center = QtPositioning.coordinate(lat, lon)
    }

    function setZoom(level) {
        map.zoomLevel = level
    }

    function updateNodes(nodes) {
        nodeModel = nodes
        nodeRepeater.model = nodes
    }

    Plugin {
        id: osmPlugin
        name: "osm"

        PluginParameter {
            name: "osm.mapping.providersrepository.disabled"
            value: "true"
        }
        PluginParameter {
            name: "osm.mapping.providersrepository.address"
            value: ""
        }
        PluginParameter {
            name: "osm.useragent"
            value: "MeshtasticClient"
        }
    }

    Map {
        id: map
        anchors.fill: parent
        plugin: osmPlugin
        center: QtPositioning.coordinate(37.7749, -122.4194) // Default: San Francisco
        zoomLevel: 10
        copyrightsVisible: true

        Component.onCompleted: {
            root.mapReady()
        }

        // Node markers
        MapItemView {
            model: nodeRepeater.model
            delegate: MapQuickItem {
                id: nodeMarker
                coordinate: QtPositioning.coordinate(
                    modelData.latitude,
                    modelData.longitude
                )
                anchorPoint.x: markerItem.width / 2
                anchorPoint.y: markerItem.height

                sourceItem: Item {
                    id: markerItem
                    width: 32
                    height: 40

                    // Marker pin shape
                    Rectangle {
                        id: pinBody
                        width: 24
                        height: 24
                        radius: 12
                        color: getNodeColor(modelData)
                        border.color: "#333333"
                        border.width: 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top

                        // Short name label
                        Text {
                            anchors.centerIn: parent
                            text: modelData.shortName ? modelData.shortName.substring(0, 2) : "?"
                            color: "white"
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    // Pin point
                    Rectangle {
                        width: 8
                        height: 8
                        rotation: 45
                        color: pinBody.color
                        border.color: "#333333"
                        border.width: 1
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: pinBody.bottom
                        anchors.topMargin: -6
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.nodeClicked(modelData.nodeNum)
                            tooltip.visible = !tooltip.visible
                        }
                    }

                    // Tooltip
                    Rectangle {
                        id: tooltip
                        visible: false
                        width: tooltipText.width + 16
                        height: tooltipText.height + 8
                        color: "#333333"
                        radius: 4
                        anchors.bottom: pinBody.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 4

                        Text {
                            id: tooltipText
                            anchors.centerIn: parent
                            text: buildTooltip(modelData)
                            color: "white"
                            font.pixelSize: 11
                        }
                    }

                    function buildTooltip(node) {
                        var lines = []
                        if (node.longName) {
                            lines.push(node.longName)
                        }
                        if (node.nodeId) {
                            lines.push(node.nodeId)
                        }
                        if (node.altitude) {
                            lines.push("Alt: " + node.altitude + "m")
                        }
                        if (node.batteryLevel > 0) {
                            lines.push("Battery: " + node.batteryLevel + "%")
                        }
                        return lines.join("\n")
                    }

                    function getNodeColor(node) {
                        // Color by battery level or default
                        if (node.batteryLevel > 0) {
                            if (node.batteryLevel < 20) return "#FF4444"
                            if (node.batteryLevel < 50) return "#FFAA00"
                            return "#44AA44"
                        }
                        // Default color based on node number
                        var colors = ["#4A90D9", "#D94A4A", "#4AD94A", "#D9D94A", "#D94AD9", "#4AD9D9"]
                        return colors[node.nodeNum % colors.length]
                    }
                }
            }
        }

        // Gesture handling
        gesture.enabled: true
        gesture.acceptedGestures: MapGestureArea.PanGesture |
                                   MapGestureArea.FlickGesture |
                                   MapGestureArea.PinchGesture |
                                   MapGestureArea.RotationGesture |
                                   MapGestureArea.TiltGesture

        // Mouse wheel zoom
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: {
                if (wheel.angleDelta.y > 0) {
                    map.zoomLevel = Math.min(map.zoomLevel + 0.5, 19)
                } else {
                    map.zoomLevel = Math.max(map.zoomLevel - 0.5, 2)
                }
            }
        }
    }

    // Hidden repeater just to track model changes
    Repeater {
        id: nodeRepeater
        model: []
        delegate: Item {}
    }

    // Zoom controls
    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 5

        Rectangle {
            width: 30
            height: 30
            color: zoomInMouse.containsMouse ? "#e0e0e0" : "white"
            border.color: "#666666"
            radius: 4

            Text {
                anchors.centerIn: parent
                text: "+"
                font.pixelSize: 20
                font.bold: true
            }

            MouseArea {
                id: zoomInMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: map.zoomLevel = Math.min(map.zoomLevel + 1, 19)
            }
        }

        Rectangle {
            width: 30
            height: 30
            color: zoomOutMouse.containsMouse ? "#e0e0e0" : "white"
            border.color: "#666666"
            radius: 4

            Text {
                anchors.centerIn: parent
                text: "-"
                font.pixelSize: 20
                font.bold: true
            }

            MouseArea {
                id: zoomOutMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: map.zoomLevel = Math.max(map.zoomLevel - 1, 2)
            }
        }
    }

    // Scale indicator
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        width: 100
        height: 20
        color: "white"
        opacity: 0.8
        radius: 4

        Text {
            anchors.centerIn: parent
            text: "Zoom: " + map.zoomLevel.toFixed(1)
            font.pixelSize: 10
        }
    }
}
