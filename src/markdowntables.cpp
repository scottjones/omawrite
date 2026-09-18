#include "markdowntables.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QVariantMap>

namespace {

QVariantList cells(const QString &line, int offset, bool body = false) {
    QList<int> pipes;
    int backslashes = 0;
    for (int i = 0; i < line.size(); ++i) {
        if (line.at(i) == QLatin1Char('|') && backslashes % 2 == 0)
            pipes.append(i);
        backslashes = line.at(i) == QLatin1Char('\\') ? backslashes + 1 : 0;
    }
    if (!body && (pipes.isEmpty() || line.startsWith(QStringLiteral("    "))
            || line.startsWith(QLatin1Char('\t'))))
        return {};

    QList<int> boundaries{-1};
    boundaries.append(pipes);
    boundaries.append(line.size());
    int first = 0;
    int last = boundaries.size() - 1;
    if (!pipes.isEmpty() && line.left(pipes.first()).trimmed().isEmpty())
        ++first;
    if (!pipes.isEmpty() && line.mid(pipes.last() + 1).trimmed().isEmpty())
        --last;

    QVariantList result;
    for (int i = first; i < last; ++i) {
        int start = boundaries.at(i) + 1;
        int end = boundaries.at(i + 1);
        while (start < end && line.at(start).isSpace())
            ++start;
        while (end > start && line.at(end - 1).isSpace())
            --end;
        result.append(QVariantMap{{"text", line.mid(start, end - start)},
                                  {"start", offset + start}, {"end", offset + end}});
    }
    return result;
}

// Emit only text and inline styles. Images and raw HTML must not turn a local
// table preview into a resource loader, and links remain editable source.
QString inlineHtml(const QString &source, int &visibleLength) {
    QTextDocument document;
    QString content = source;
    // Table pipe escapes are consumed before inline parsing, including in code.
    content.replace(QStringLiteral("\\|"), QStringLiteral("|"));
    // Force a paragraph so "- item", "# heading", "---", and reference
    // definitions stay inline cell content. Strip this prefix from the output.
    const QString prefix = QStringLiteral("x ");
    document.setMarkdown(prefix + content, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                                 | QTextDocument::MarkdownNoHTML);
    QString html;
    visibleLength = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        if (!html.isEmpty())
            html += QStringLiteral("<br>");
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            QString text = fragment.text().mid(qMax(0, int(prefix.size()) - fragment.position()));
            if (format.isImageFormat())
                text = QStringLiteral("[image]");
            visibleLength += text.size();
            text = text.toHtmlEscaped();
            if (format.fontWeight() >= QFont::Bold)
                text = QStringLiteral("<b>") + text + QStringLiteral("</b>");
            if (format.fontItalic())
                text = QStringLiteral("<i>") + text + QStringLiteral("</i>");
            if (format.fontStrikeOut())
                text = QStringLiteral("<s>") + text + QStringLiteral("</s>");
            if (format.isAnchor())
                text = QStringLiteral("<u>") + text + QStringLiteral("</u>");
            html += text;
        }
    }
    return html;
}

QVariantList formattedCells(QVariantList row, QList<int> &lengths) {
    for (int column = 0; column < row.size(); ++column) {
        auto &value = row[column];
        auto cell = value.toMap();
        int visibleLength = 0;
        cell.insert("html", inlineHtml(cell.value("text").toString(), visibleLength));
        lengths[column] += visibleLength;
        value = cell;
    }
    return row;
}

QVariantList scanTables(const QString &text, bool firstOnly) {
    const auto lines = text.split(QLatin1Char('\n'));
    QList<int> offsets;
    int offset = 0;
    for (const auto &line : lines) {
        offsets.append(offset);
        offset += line.size() + 1;
    }
    static const QRegularExpression fenceRe(QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$"));
    static const QRegularExpression separatorRe(QStringLiteral("^:?-+:?$"));
    static const QRegularExpression blockStartRe(
        QStringLiteral("^ {0,3}(?:#{1,6}(?:\\s|$)|>|[-+*][ \\t]+|1[.)][ \\t]+|`{3,}|~{3,})"));
    static const QRegularExpression ruleRe(QStringLiteral("^ {0,3}([-*_])(?:[ \\t]*\\1){2,}[ \\t]*$"));
    QString fence;
    QVariantList tables;
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const auto match = fenceRe.match(lines.at(i));
        if (match.hasMatch()) {
            const auto marker = match.captured(1);
            if (fence.isEmpty())
                fence = marker;
            else if (marker.front() == fence.front() && marker.size() >= fence.size()
                     && match.captured(2).trimmed().isEmpty())
                fence.clear();
            continue;
        }
        if (!fence.isEmpty())
            continue;

        const auto header = cells(lines.at(i), offsets.at(i));
        const auto separator = cells(lines.at(i + 1), offsets.at(i + 1));
        if (header.isEmpty() || separator.size() != header.size())
            continue;
        QVariantList alignments;
        bool valid = true;
        for (const auto &value : separator) {
            const auto cell = value.toMap().value("text").toString();
            if (!separatorRe.match(cell).hasMatch()) {
                valid = false;
                break;
            }
            alignments.append(cell.endsWith(QLatin1Char(':'))
                ? (cell.startsWith(QLatin1Char(':')) ? "center" : "right") : "left");
        }
        if (!valid)
            continue;

        const int start = offsets.at(i);
        // The footer only needs to know whether a table exists. Avoid parsing
        // every body cell and creating QTextDocuments after each typing pause.
        if (firstOnly)
            return {QVariantMap{{"start", start}}};
        int end = offsets.at(i + 1) + lines.at(i + 1).size();
        QVariantList rows;
        QList<int> lengths(header.size(), 0);
        QList<int> headingLengths(header.size(), 0);
        const auto formattedHeader = formattedCells(header, headingLengths);
        ++i;
        while (i + 1 < lines.size()) {
            const auto &line = lines.at(i + 1);
            if (line.trimmed().isEmpty() || blockStartRe.match(line).hasMatch()
                    || ruleRe.match(line).hasMatch())
                break;
            auto row = cells(line, offsets.at(i + 1), true);
            // GFM pads short body rows and ignores excess cells for display.
            // Source ranges still refer to the untouched Markdown.
            row = row.mid(0, header.size());
            const int rowEnd = offsets.at(i + 1) + line.size();
            while (row.size() < header.size())
                row.append(QVariantMap{{"text", QString()}, {"start", rowEnd}, {"end", rowEnd}});
            rows.append(QVariantMap{{"cells", formattedCells(row, lengths)}});
            ++i;
            end = offsets.at(i) + lines.at(i).size();
        }
        QVariantList weights;
        for (int column = 0; column < header.size(); ++column) {
            const int average = rows.isEmpty() ? 0 : lengths.at(column) / rows.size();
            const int heading = headingLengths.at(column);
            weights.append(qBound(12, qMax(heading, average), 60));
        }
        tables.append(QVariantMap{{"start", start}, {"end", end},
                                 {"header", formattedHeader}, {"rows", rows},
                                 {"alignments", alignments}, {"weights", weights}});
    }
    return tables;
}

}

QVariantList MarkdownTables::parse(const QString &text) {
    return scanTables(text, false);
}

bool MarkdownTables::containsTable(const QString &text) {
    return !scanTables(text, true).isEmpty();
}
