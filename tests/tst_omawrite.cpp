#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>

#include "backend.h"
#include "markdownhighlighter.h"
#include "markdowntables.h"

namespace {

QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *found = visualItem(child, name))
            return found;
    }
    return nullptr;
}

struct TablePreviewHarness {
    QStringList tableWarnings;
    Backend backend;
    QQmlEngine engine;
    QQmlComponent component{&engine};
    QScopedPointer<QObject> root;
    QQuickWindow *window = nullptr;
    QQuickItem *editor = nullptr;
    QObject *dialog = nullptr;

    bool load(const QString &text) {
        QObject::connect(&engine, &QQmlEngine::warnings, &engine, [this](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) {
                if (warning.url().fileName() == "TablePreviewDialog.qml"
                        || warning.url().fileName() == "MarkdownTableRow.qml")
                    tableWarnings.append(warning.toString());
            }
        });
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        component.loadUrl(QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        if (!component.isReady())
            return false;
        root.reset(component.create());
        window = qobject_cast<QQuickWindow *>(root.data());
        if (!window)
            return false;
        editor = window->findChild<QQuickItem *>(QStringLiteral("sourceEditor"));
        dialog = window->findChild<QObject *>(QStringLiteral("tablePreviewDialog"));
        if (!editor || !dialog)
            return false;
        editor->setProperty("text", text);
        window->requestActivate();
        editor->forceActiveFocus();
        return true;
    }

    QQuickItem *item(const QString &name) {
        return visualItem(window->contentItem(), name);
    }

    void click(QQuickItem *item, const QPointF &position = QPointF(-1, -1)) {
        const auto local = position.x() < 0 ? QPointF(item->width() / 2, item->height() / 2) : position;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(local).toPoint());
    }

    void openPreview() {
        QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    }
};

}

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        qputenv("XDG_DATA_HOME", m_settingsDirectory.path().toUtf8());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void parsesTablesWithSourcePositions() {
        const QString source = QStringLiteral("Before \U0001F642\n\n"
            "| Area | State | Count |\n| :--- | :---: | ---: |\n"
            "| one \\| two | `a\\|b` | 42 |\n"
            "| next | **ready** | 5 |\n\nAfter");
        const auto tables = MarkdownTables::parse(source);
        QCOMPARE(tables.size(), 1);
        const auto table = tables.first().toMap();
        QCOMPARE(table.value("alignments").toList(), QVariantList({"left", "center", "right"}));
        QCOMPARE(source.mid(table.value("start").toInt(), 6), QStringLiteral("| Area"));
        const auto rows = table.value("rows").toList();
        QCOMPARE(rows.size(), 2);
        const auto cells = rows.first().toMap().value("cells").toList();
        QCOMPARE(cells.size(), 3);
        for (const auto &value : cells) {
            const auto cell = value.toMap();
            QCOMPARE(source.mid(cell.value("start").toInt(),
                                cell.value("end").toInt() - cell.value("start").toInt()),
                     cell.value("text").toString());
        }
        QCOMPARE(cells.at(0).toMap().value("html").toString(), QStringLiteral("one | two"));
        QCOMPARE(cells.at(1).toMap().value("html").toString(), QStringLiteral("a|b"));
        QVERIFY(source.mid(table.value("end").toInt()).startsWith(QStringLiteral("\n\nAfter")));
    }

    void ignoresTablesInCodeAndMalformedHeaders() {
        const QString table = QStringLiteral("A | B\n--- | ---\nx | y\n");
        QCOMPARE(MarkdownTables::parse(table).size(), 1);
        QCOMPARE(MarkdownTables::parse("````md\n```\n" + table + "````\n\n" + table).size(), 1);
        QVERIFY(MarkdownTables::parse("~~~\n" + table).isEmpty());
        QVERIFY(MarkdownTables::parse("    A | B\n    --- | ---\n    x | y").isEmpty());
        QVERIFY(MarkdownTables::parse("A | B\n--- | nope\nx | y").isEmpty());
        QVERIFY(MarkdownTables::parse("A | B | C\n--- | ---\nx | y").isEmpty());
        QCOMPARE(MarkdownTables::parse(table + "\n" + table).size(), 2);

        const auto safe = MarkdownTables::parse(QStringLiteral(
            "A | B\n--- | ---\n<img src=\"https://example.com/image\"> | **bold** & text\n"));
        const auto cells = safe.first().toMap().value("rows").toList().first().toMap()
                              .value("cells").toList();
        QVERIFY(!cells.first().toMap().value("html").toString().contains("<img"));
        QCOMPARE(cells.last().toMap().value("html").toString(), QStringLiteral("<b>bold</b> &amp; text"));
    }

    void rendersOnlyInlineMarkdownInCells_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("html");
        QTest::newRow("bullet") << QStringLiteral("- item") << QStringLiteral("- item");
        QTest::newRow("number") << QStringLiteral("1. item") << QStringLiteral("1. item");
        QTest::newRow("heading") << QStringLiteral("# heading") << QStringLiteral("# heading");
        QTest::newRow("quote") << QStringLiteral("> quote") << QStringLiteral("&gt; quote");
        QTest::newRow("rule") << QStringLiteral("---") << QStringLiteral("---");
        QTest::newRow("reference") << QStringLiteral("[label]: target") << QStringLiteral("[label]: target");
        QTest::newRow("inline styles") << QStringLiteral("**bold** and *italic*") << QStringLiteral("<b>bold</b> and <i>italic</i>");
        QTest::newRow("code") << QStringLiteral("`*literal* & <tag>`") << QStringLiteral("*literal* &amp; &lt;tag&gt;");
        QTest::newRow("image") << QStringLiteral("![description](https://example.com/image)") << QStringLiteral("[image]");
        QTest::newRow("html") << QStringLiteral("<img src='file:///tmp/image'>") << QStringLiteral("&lt;img src='file:///tmp/image'&gt;");
        QTest::newRow("empty") << QStringLiteral("") << QStringLiteral("");
    }

    void rendersOnlyInlineMarkdownInCells() {
        QFETCH(QString, source);
        QFETCH(QString, html);
        const auto tables = MarkdownTables::parse("| Header |\n| --- |\n| " + source + " |\n");
        QCOMPARE(tables.size(), 1);
        const auto rows = tables.first().toMap().value("rows").toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("cells").toList().first().toMap().value("html").toString(), html);
    }

    void keepsRowsWithMissingOrExtraCells() {
        const QString source = QStringLiteral("A | B\n- | :-:\nfirst | good\nmissing\n"
                                               "extra | content | preserved in source\nlast | valid\n\nAfter");
        const auto tables = MarkdownTables::parse(source);
        QCOMPARE(tables.size(), 1);
        const auto table = tables.first().toMap();
        const auto rows = table.value("rows").toList();
        QCOMPARE(rows.size(), 4);
        for (const auto &row : rows)
            QCOMPARE(row.toMap().value("cells").toList().size(), 2);
        const auto missing = rows.at(1).toMap().value("cells").toList().at(1).toMap();
        QCOMPARE(missing.value("text").toString(), QString());
        QCOMPARE(missing.value("start").toInt(), source.indexOf("missing") + 7);
        QCOMPARE(missing.value("end"), missing.value("start"));
        QCOMPARE(rows.at(2).toMap().value("cells").toList().at(1).toMap().value("text").toString(),
                 QStringLiteral("content"));
        QCOMPARE(rows.last().toMap().value("cells").toList().last().toMap().value("text").toString(),
                 QStringLiteral("valid"));
        QCOMPARE(source.mid(table.value("end").toInt()), QStringLiteral("\n\nAfter"));
    }

    void previewsTablesWithoutChangingDocument() {
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        auto *dialog = window->findChild<QObject *>(QStringLiteral("tablePreviewDialog"));
        auto *button = window->findChild<QObject *>(QStringLiteral("tablesButton"));
        QVERIFY(editor);
        QVERIFY(dialog);
        QVERIFY(button);
        const QString source = QStringLiteral("Before\n\n| Area | State |\n| --- | --- |\n"
                                               "| Branches | ready |\n\nAfter\n");
        editor->setProperty("text", source);
        QTRY_VERIFY(button->property("visible").toBool());
        QTemporaryDir directory;
        const auto path = directory.filePath("table.md");
        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(editor->property("text").toString(), source);
        QVERIFY(!backend.modified());

        const int start = source.indexOf("ready");
        QVERIFY(QMetaObject::invokeMethod(dialog, "editCell",
            Q_ARG(QVariant, start), Q_ARG(QVariant, start + 5)));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(editor->property("selectedText").toString(), QStringLiteral("ready"));
        QVERIFY(QMetaObject::invokeMethod(editor, "replaceSelectionWith", Q_ARG(QVariant, "changed")));
        QVERIFY(backend.modified());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "showTables"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        const auto table = dialog->property("currentTable").toMap();
        QCOMPARE(table.value("rows").toList().first().toMap().value("cells").toList().last()
                     .toMap().value("text").toString(), QStringLiteral("changed"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        backend.save();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), QString(source).replace("ready", "changed"));
    }

    void keepsTableButtonLabelReadable_data() {
        QTest::addColumn<int>("scale");
        QTest::addColumn<int>("width");
        QTest::newRow("desktop") << 1 << 1280;
        QTest::newRow("narrow") << 1 << 720;
        QTest::newRow("large text") << 2 << 1280;
        QTest::newRow("large text narrow") << 2 << 720;
    }

    void keepsTableButtonLabelReadable() {
        QFETCH(int, scale);
        QFETCH(int, width);
        TablePreviewHarness ui;
        QVERIFY2(ui.load("A | B\n--- | ---\nx | y\n"), qPrintable(ui.component.errorString()));
        ui.window->resize(width, 820);
        ui.backend.setTextScale(scale);
        QTRY_VERIFY(ui.item("tablesButton") && ui.item("tablesButton")->isVisible());
        auto *button = ui.item("tablesButton");
        QCOMPARE(button->property("text").toString(), QStringLiteral("Tables"));
        QVERIFY(button->property("implicitContentHeight").toReal() > 0);
        QVERIFY(button->property("availableHeight").toReal()
                    >= button->property("implicitContentHeight").toReal());
        QVERIFY(button->property("availableWidth").toReal()
                    >= button->property("implicitContentWidth").toReal());
        qreal opacity = 1;
        for (auto *item = button; item; item = item->parentItem())
            opacity *= item->opacity();
        QVERIFY(opacity >= 0.9);
        const auto bounds = button->mapRectToScene(button->boundingRect());
        QVERIFY(bounds.left() >= 0 && bounds.right() <= ui.window->width());
        QVERIFY(bounds.top() >= 0 && bounds.bottom() <= ui.window->height());
        ui.click(button);
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
    }

    void editsTableThroughMouseAndKeyboardWithUndo() {
        const QString source = QStringLiteral("Before \U0001F642\n\n| Area | State |\n| --- | --- |\n| short | ")
            + QStringLiteral("Long wrapping content ").repeated(15) + QStringLiteral("|\n\nAfter\n");
        TablePreviewHarness ui;
        QVERIFY2(ui.load(source), qPrintable(ui.component.errorString()));
        QTRY_VERIFY(ui.window->isActive());
        QTRY_VERIFY(ui.item("tablesButton") && ui.item("tablesButton")->isVisible());
        ui.click(ui.item("tablesButton"));
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QTRY_VERIFY(ui.item("tableCell_0_0"));
        auto *cell = ui.item("tableCell_0_0");
        QVERIFY(cell->height() > 70);
        // The blank space beneath a short cell is part of its click target.
        ui.click(cell, QPointF(16, cell->height() - 8));
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectedText").toString(), QStringLiteral("short"));
        QCOMPARE(ui.editor->property("selectionStart").toInt(), source.indexOf("short"));
        for (char character : QByteArray("changed"))
            QTest::keyClick(ui.window, character);
        const QString changed = QString(source).replace("short", "changed");
        QCOMPARE(ui.editor->property("text").toString(), changed);

        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(ui.editor->property("text").toString(), source);
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(ui.editor->property("text").toString(), changed);
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QCOMPARE(ui.dialog->property("currentTable").toMap().value("rows").toList().first()
                     .toMap().value("cells").toList().first().toMap().value("text").toString(),
                 QStringLiteral("changed"));
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void navigatesTablesAndResetsScroll() {
        QString source = "A | B\n--- | ---\n";
        for (int i = 0; i < 80; ++i)
            source += QStringLiteral("first %1 | row\n").arg(i);
        source += "\nC | D\n--- | ---\nsecond | row\n\nE | F\n--- | ---\nthird | row\n";
        TablePreviewHarness ui;
        QVERIFY2(ui.load(source), qPrintable(ui.component.errorString()));
        QTRY_VERIFY(ui.window->isActive());
        ui.editor->setProperty("cursorPosition", source.indexOf("second"));
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QCOMPARE(ui.dialog->property("tableIndex").toInt(), 1);
        QVERIFY(ui.item("nextTableButton"));
        ui.click(ui.item("nextTableButton"));
        QCOMPARE(ui.dialog->property("tableIndex").toInt(), 2);
        QVERIFY(!ui.item("nextTableButton")->isEnabled());
        ui.click(ui.item("previousTableButton"));
        ui.click(ui.item("previousTableButton"));
        QCOMPARE(ui.dialog->property("tableIndex").toInt(), 0);
        QVERIFY(!ui.item("previousTableButton")->isEnabled());
        auto *rows = ui.item("tableRowsView");
        QVERIFY(rows);
        rows->setProperty("contentY", 400);
        QVERIFY(rows->property("contentY").toReal() > 0);
        ui.click(ui.item("nextTableButton"));
        QTRY_VERIFY(ui.item("tableCell_0_0"));
        ui.click(ui.item("tableCell_0_0"));
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectedText").toString(), QStringLiteral("second"));

        ui.editor->setProperty("cursorPosition", source.indexOf("first"));
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QVERIFY(ui.item("tableRowsView")->property("atYBeginning").toBool());
        QVERIFY(ui.item("editTableMarkdownButton")->hasActiveFocus());
        QTest::keyClick(ui.window, Qt::Key_Space); // Activate the focused edit button.
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("cursorPosition").toInt(), 0);
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void invalidatesPreviewOnEditsAndFileReload() {
        const QString original = "A | B\n--- | ---\nold | value\n";
        const QString updated = "A longer introduction\n\nA | B\n--- | ---\nnew | value\n";
        TablePreviewHarness ui;
        QVERIFY2(ui.load(original), qPrintable(ui.component.errorString()));
        QTRY_VERIFY(ui.window->isActive());
        QTemporaryDir directory;
        const auto path = directory.filePath("tables.md");
        ui.backend.saveAs(QUrl::fromLocalFile(path));
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(updated.toUtf8());
        file.close();
        ui.backend.reloadFromDisk();
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.dialog->property("tables").toList().size(), 0);
        const int cursor = ui.editor->property("cursorPosition").toInt();
        QVERIFY(QMetaObject::invokeMethod(ui.dialog, "editCell", Q_ARG(QVariant, 10), Q_ARG(QVariant, 14)));
        QCOMPARE(ui.editor->property("cursorPosition").toInt(), cursor);
        QCOMPARE(ui.editor->property("text").toString(), updated);

        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QTRY_VERIFY(ui.item("tableCell_0_0"));
        ui.click(ui.item("tableCell_0_0"));
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectionStart").toInt(), updated.indexOf("new"));
        QCOMPARE(ui.editor->property("selectedText").toString(), QStringLiteral("new"));
        QTest::keyClick(ui.window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(ui.window, Qt::Key_Backspace);
        QTRY_VERIFY(!ui.window->property("hasTables").toBool());
        ui.openPreview();
        QVERIFY(!ui.dialog->property("visible").toBool());
        QTest::keyClick(ui.window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(ui.editor->property("text").toString(), updated);
        QTRY_VERIFY(ui.window->property("hasTables").toBool());
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void laysOutWideTables_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("scale");
        QTest::newRow("narrow") << 720 << 1;
        QTest::newRow("large text") << 720 << 2;
        QTest::newRow("desktop") << 1280 << 1;
    }

    void sizesColumnsByVisibleText() {
        const QString plain = "Contribution | Status\n--- | ---\nApple Silicon foundation | Open\n";
        const QString linked = QString(plain).replace("Apple Silicon foundation",
            "[Apple Silicon foundation](https://example.com/" + QString(200, 'x') + ")");
        QCOMPARE(MarkdownTables::parse(linked).first().toMap().value("weights"),
                 MarkdownTables::parse(plain).first().toMap().value("weights"));
    }

    void fitsUnevenColumnsWithoutHorizontalScrolling_data() {
        QTest::addColumn<int>("scale");
        QTest::newRow("normal text") << 1;
        QTest::newRow("large text") << 2;
    }

    void fitsUnevenColumnsWithoutHorizontalScrolling() {
        QFETCH(int, scale);
        const QString source = "Contribution | Upstream source branch and checked head | Upstream status | In the collaboration\n"
            "--- | --- | --- | ---\n[Apple Silicon foundation](https://example.com/"
            + QString(200, 'x') + ") | contributor:omacom/asahi-overlay 980ce7eb | Open | "
            + QStringLiteral("Rebased with architecture and package availability changes. ").repeated(4) + "\n";
        TablePreviewHarness ui;
        QVERIFY2(ui.load(source), qPrintable(ui.component.errorString()));
        ui.window->resize(1280, 820);
        ui.backend.setTextScale(scale);
        QTRY_VERIFY(ui.window->isActive());
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        auto *scroll = ui.item("tableHorizontalScroll");
        QVERIFY(scroll);
        QCOMPARE(scroll->property("contentWidth").toReal(), scroll->width());
        for (int column = 0; column < 4; ++column) {
            auto *cell = ui.item("tableCell_0_" + QString::number(column));
            QVERIFY(cell);
            const auto left = cell->mapToItem(scroll, QPointF(0, 0)).x();
            QVERIFY(left >= -1);
            QVERIFY(left + cell->width() <= scroll->width() + 1);
            QVERIFY(cell->width() >= scale * 16 * 8);
        }
        // A viewport resize must recalculate widths while the preview is open.
        ui.window->resize(1440, 820);
        QTRY_COMPARE(scroll->property("contentWidth").toReal(), scroll->width());
        ui.click(ui.item("tableCell_0_3"), QPointF(16, 16));
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectedText").toString(),
                 QStringLiteral("Rebased with architecture and package availability changes. ").repeated(4).trimmed());
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void laysOutWideTables() {
        QFETCH(int, width);
        QFETCH(int, scale);
        const QString source = "A | B | C | D | E | F\n--- | --- | --- | --- | --- | ---\nshort | "
            + QString(300, 'x') + " | c | d | e | last\n\nA | B\n--- | ---\nother | table\n";
        TablePreviewHarness ui;
        QVERIFY2(ui.load(source), qPrintable(ui.component.errorString()));
        ui.window->resize(width, 520);
        ui.backend.setTextScale(scale);
        QTRY_VERIFY(ui.window->isActive());
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QTRY_VERIFY(ui.item("tableCell_0_1"));
        auto *header = ui.item("tablePreviewHeader");
        QVERIFY(header);
        for (const auto &name : {"previousTableButton", "nextTableButton", "editTableMarkdownButton", "closeTableButton"}) {
            auto *button = ui.item(name);
            QVERIFY(button);
            const auto topLeft = button->mapToItem(header, QPointF(0, 0));
            QVERIFY(topLeft.x() >= 0 && topLeft.y() >= 0);
            QVERIFY(topLeft.x() + button->width() <= header->width() + 1);
            QVERIFY(topLeft.y() + button->height() <= header->height() + 1);
        }
        auto *longCell = ui.item("tableCell_0_1");
        auto *shortCell = ui.item("tableCell_0_0");
        QVERIFY(longCell->height() > scale * 40);
        QCOMPARE(shortCell->height(), longCell->height());
        auto *scroll = ui.item("tableHorizontalScroll");
        QVERIFY(scroll);
        if (width == 1280)
            QCOMPARE(scroll->property("contentWidth").toReal(), scroll->width());
        else
            QVERIFY(scroll->property("contentWidth").toReal() > scroll->width());
        scroll->setProperty("contentX", scroll->property("contentWidth").toReal() - scroll->width());
        auto *lastCell = ui.item("tableCell_0_5");
        QVERIFY(lastCell);
        ui.click(lastCell, QPointF(lastCell->width() / 2, 16));
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectedText").toString(), QStringLiteral("last"));
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void stopsTablesAtBlockBoundaries_data() {
        QTest::addColumn<QString>("following");
        QTest::newRow("paragraph") << QStringLiteral("\nFollowing paragraph");
        QTest::newRow("heading") << QStringLiteral("# Heading\nText");
        QTest::newRow("list") << QStringLiteral("- list item\n- second item");
        QTest::newRow("fence") << QStringLiteral("```\nnot | a table\n```\n");
        QTest::newRow("rule") << QStringLiteral("***\nText");
    }

    void stopsTablesAtBlockBoundaries() {
        QFETCH(QString, following);
        const QString table = "A | B\n--- | ---\nfirst | row";
        const auto result = MarkdownTables::parse(table + '\n' + following);
        QCOMPARE(result.size(), 1);
        QCOMPARE(result.first().toMap().value("rows").toList().size(), 1);
        QCOMPARE(result.first().toMap().value("end").toInt(), table.size());
    }

    void detectsTablesWithoutRendering_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<bool>("expected");
        QTest::newRow("prose") << QStringLiteral("Just | some prose\nwithout a delimiter") << false;
        QTest::newRow("fenced") << QStringLiteral("~~~md\nA | B\n--- | ---\nx | y\n~~~") << false;
        QTest::newRow("after fence") << QStringLiteral("```\ncode\n```\nA | B\n- | -\nx | y") << true;
        QTest::newRow("empty table") << QStringLiteral("| A |\n| --- |") << true;
        QTest::newRow("empty header") << QStringLiteral("| | |\n| --- | --- |\n| | |") << true;
        QTest::newRow("windows newlines") << QStringLiteral("A | B\r\n--- | ---\r\nx | y\r\n") << true;
        QTest::newRow("indented") << QStringLiteral("    A | B\n    --- | ---\n    x | y") << false;
    }

    void detectsTablesWithoutRendering() {
        QFETCH(QString, source);
        QFETCH(bool, expected);
        QCOMPARE(MarkdownTables::containsTable(source), expected);
    }

    void keepsLongPreviewsBoundedAndReachesLastRow() {
        QString source = "A | B\n--- | ---\n";
        for (int i = 0; i < 1000; ++i)
            source += QStringLiteral("row %1 | body text\n").arg(i);
        TablePreviewHarness ui;
        QVERIFY2(ui.load(source), qPrintable(ui.component.errorString()));
        QTRY_VERIFY(ui.window->isActive());
        ui.openPreview();
        QTRY_VERIFY(ui.dialog->property("opened").toBool());
        QTRY_VERIFY(ui.item("tableCell_0_0"));
        // A long document must not instantiate every row just to show its top.
        QVERIFY(!ui.item("tableCell_999_0"));
        auto *rows = ui.item("tableRowsView");
        QVERIFY(rows);
        QVERIFY(QMetaObject::invokeMethod(rows, "positionViewAtEnd"));
        QTRY_VERIFY(ui.item("tableCell_999_0"));
        auto *last = ui.item("tableCell_999_0");
        ui.click(last);
        QTRY_VERIFY(!ui.dialog->property("visible").toBool());
        QCOMPARE(ui.editor->property("selectedText").toString(), QStringLiteral("row 999"));
        QCOMPARE(ui.editor->property("selectionStart").toInt(), source.indexOf("row 999"));
        QCOMPARE(ui.editor->property("text").toString(), source);
        QVERIFY2(ui.tableWarnings.isEmpty(), qPrintable(ui.tableWarnings.join('\n')));
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
