import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    objectName: "tablePreviewDialog"
    property var tables: []
    property int tableIndex: 0
    property int sourceRevision: -1
    readonly property var currentTable: tableIndex >= 0 && tables.length > tableIndex ? tables[tableIndex] : null
    property color pageColor
    property color textColor
    property int fontPixelSize: 16
    font.family: "iA Writer Mono S"
    font.pixelSize: fontPixelSize
    property int containerWidth: 1280
    property int containerHeight: 820
    readonly property color panelColor: Qt.rgba(
        pageColor.r * 0.94 + textColor.r * 0.06,
        pageColor.g * 0.94 + textColor.g * 0.06,
        pageColor.b * 0.94 + textColor.b * 0.06, 1)
    readonly property color ruleColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.2)
    signal editRequested(int start, int end)

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(containerWidth - 48, fontPixelSize * 76)
    height: Math.min(containerHeight - 64,
                    Math.max(fontPixelSize * 8, bodyScroll.contentHeight
                        + (bodyScroll.headerItem ? bodyScroll.headerItem.height : 0))
                        + header.implicitHeight + topPadding + bottomPadding)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 16
    onTableIndexChanged: resetScroll()
    onOpened: {
        resetScroll();
        editButton.forceActiveFocus();
    }

    function resetScroll() {
        horizontalScroll.contentX = 0;
        bodyScroll.positionViewAtBeginning();
    }

    function invalidate() {
        close();
        sourceRevision = -1;
        tables = [];
        tableIndex = 0;
    }

    function editCell(start, end) {
        if (sourceRevision < 0)
            return;
        close();
        editRequested(start, end);
    }

    background: Rectangle {
        color: dialog.pageColor
        border.color: dialog.ruleColor
    }

    header: Flow {
        objectName: "tablePreviewHeader"
        spacing: 8
        padding: 12

        Label {
            text: dialog.tables.length > 1
                ? "Table " + (dialog.tableIndex + 1) + " of " + dialog.tables.length : "Table"
            color: dialog.textColor
            font.family: "iA Writer Mono S"
            font.pixelSize: dialog.fontPixelSize
            padding: 12
        }
        Button {
            objectName: "previousTableButton"
            text: "Previous"
            visible: dialog.tables.length > 1
            enabled: dialog.tableIndex > 0
            onClicked: dialog.tableIndex--
        }
        Button {
            objectName: "nextTableButton"
            text: "Next"
            visible: dialog.tables.length > 1
            enabled: dialog.tableIndex + 1 < dialog.tables.length
            onClicked: dialog.tableIndex++
        }
        Button {
            id: editButton
            objectName: "editTableMarkdownButton"
            text: "Edit Markdown"
            enabled: dialog.currentTable !== null
            onClicked: dialog.editCell(dialog.currentTable.start, dialog.currentTable.start)
        }
        Button {
            objectName: "closeTableButton"
            text: "Close"
            onClicked: dialog.close()
        }
    }

    contentItem: Flickable {
        id: horizontalScroll
        objectName: "tableHorizontalScroll"
        clip: true
        readonly property var weights: dialog.currentTable ? dialog.currentTable.weights : []
        readonly property real minimumWidth: dialog.fontPixelSize * 8 * weights.length
        contentWidth: Math.max(width, minimumWidth)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { }

        ListView {
            id: bodyScroll
            objectName: "tableRowsView"
            width: horizontalScroll.contentWidth
            height: horizontalScroll.height
            clip: true
            model: dialog.currentTable ? dialog.currentTable.rows : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                parent: horizontalScroll
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }

            header: MarkdownTableRow {
                width: bodyScroll.width
                cells: dialog.currentTable ? dialog.currentTable.header : []
                weights: dialog.currentTable ? dialog.currentTable.weights : []
                alignments: dialog.currentTable ? dialog.currentTable.alignments : []
                heading: true
                textColor: dialog.textColor
                panelColor: dialog.panelColor
                ruleColor: dialog.ruleColor
                fontPixelSize: dialog.fontPixelSize
                onEditRequested: (start, end) => dialog.editCell(start, end)
            }

            delegate: MarkdownTableRow {
                required property var modelData
                required property int index
                rowIndex: index
                width: bodyScroll.width
                cells: modelData.cells
                weights: dialog.currentTable ? dialog.currentTable.weights : []
                alignments: dialog.currentTable ? dialog.currentTable.alignments : []
                textColor: dialog.textColor
                panelColor: dialog.panelColor
                ruleColor: dialog.ruleColor
                fontPixelSize: dialog.fontPixelSize
                onEditRequested: (start, end) => dialog.editCell(start, end)
            }
        }
    }
}
