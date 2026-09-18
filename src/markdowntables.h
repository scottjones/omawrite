#pragma once

#include <QString>
#include <QVariantList>

namespace MarkdownTables {
QVariantList parse(const QString &text);
bool containsTable(const QString &text);
}
