#pragma once

#include <QString>
#include <QVariantList>

namespace MarkdownTables {
QVariantList parse(const QString &text);
QVariantList ranges(const QString &text);
}
