#pragma once

#include <QColor>
#include <QtCore/Qt>

class QApplication;
class QLabel;
class QLayout;
class QTreeWidget;
class QWidget;

namespace wds::studio::theme {

namespace color {
#define WDS_THEME_COLOR(name, hex) inline const QColor name{hex};
#include "colors.inc"
#undef WDS_THEME_COLOR
}  // namespace color

inline constexpr int tight_spacing = 2;
inline constexpr int compact_spacing = 4;
inline constexpr int group_spacing = 8;

void apply(QApplication& app);
void configurePanelLayout(QLayout& layout);
void configurePanelTree(QTreeWidget& tree, Qt::TextElideMode elide);
QLabel* makeEmptyState(QWidget* parent);

}  // namespace wds::studio::theme
