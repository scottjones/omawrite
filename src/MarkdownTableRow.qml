import QtQuick
import QtQuick.Controls

Item {
    id: row
    required property var cells
    required property var weights
    required property var alignments
    property int rowIndex: -1
    property bool heading: false
    property color textColor
    property color panelColor
    property color ruleColor
    property int fontPixelSize: 16
    readonly property real cellPadding: fontPixelSize * 0.8
    readonly property real weightTotal: weights.reduce((sum, value) => sum + value, 0)
    readonly property real minimumCellWidth: fontPixelSize * 8
    readonly property real extraWidth: Math.max(0, width - minimumCellWidth * weights.length)
    property real naturalHeight: 0
    signal editRequested(int start, int end)

    implicitHeight: naturalHeight + 1

    function updateHeight() {
        var tallest = fontPixelSize * 1.4 + cellPadding * 2;
        for (var i = 0; i < cellRepeater.count; ++i) {
            var cell = cellRepeater.itemAt(i);
            if (cell)
                tallest = Math.max(tallest, cell.implicitHeight);
        }
        naturalHeight = tallest;
    }

    Rectangle {
        anchors.fill: parent
        color: row.panelColor
        visible: row.heading
    }

    Row {
        id: contents
        width: parent.width
        height: row.naturalHeight

        Repeater {
            id: cellRepeater
            model: row.cells
            onItemAdded: row.updateHeight()
            onItemRemoved: Qt.callLater(row.updateHeight)

            Item {
                required property var modelData
                required property int index
                objectName: "tableCell_" + row.rowIndex + "_" + index
                // Reserve a readable minimum per column, then share only the
                // remaining viewport width according to visible text length.
                width: row.minimumCellWidth + row.extraWidth * row.weights[index] / row.weightTotal
                implicitHeight: label.implicitHeight + row.cellPadding * 2
                height: row.naturalHeight
                onImplicitHeightChanged: row.updateHeight()

                Text {
                    id: label
                    x: row.cellPadding
                    y: row.cellPadding
                    width: parent.width - row.cellPadding * 2
                    text: modelData.html
                    textFormat: Text.StyledText
                    color: row.textColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: row.fontPixelSize
                    font.bold: row.heading
                    lineHeight: 1.4
                    wrapMode: Text.Wrap
                    horizontalAlignment: row.alignments[index] === "right" ? Text.AlignRight
                        : row.alignments[index] === "center" ? Text.AlignHCenter : Text.AlignLeft
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.IBeamCursor
                    onClicked: row.editRequested(modelData.start, modelData.end)
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: row.ruleColor
    }
}
