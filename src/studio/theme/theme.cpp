#include "theme.hpp"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QStyleFactory>
#include <QTreeWidget>

namespace wds::studio::theme {

void apply(QApplication& app) {
  app.setStyle(QStyleFactory::create("Fusion"));
  auto palette = app.palette();
  palette.setColor(QPalette::Window, color::window);
  palette.setColor(QPalette::Base, color::input);
  palette.setColor(QPalette::AlternateBase, color::alternate);
  palette.setColor(QPalette::Button, color::button);
  palette.setColor(QPalette::Mid, color::border);
  palette.setColor(QPalette::Highlight, color::selected);
  palette.setColor(QPalette::Inactive, QPalette::Highlight, color::selected_inactive);
  palette.setColor(QPalette::HighlightedText, color::selected_text);
  palette.setColor(QPalette::PlaceholderText, color::secondary_text);
  palette.setColor(QPalette::ToolTipBase, color::panel);
  palette.setColor(QPalette::ToolTipText, color::text);
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(role, color::text);
    palette.setColor(QPalette::Disabled, role, color::disabled_text);
  }
  app.setPalette(palette);

  QFile source(":/studio/theme/workbench.qss");
  if (!source.open(QIODevice::ReadOnly)) return;
  auto stylesheet = QString::fromUtf8(source.readAll());
#define WDS_THEME_COLOR(name, hex) \
  stylesheet.replace(QStringLiteral("@" #name "@"), QStringLiteral(hex));
#include "colors.inc"
#undef WDS_THEME_COLOR
  app.setStyleSheet(stylesheet);
}

void configurePanelLayout(QLayout& layout) {
  layout.setContentsMargins(compact_spacing, compact_spacing, compact_spacing, compact_spacing);
  layout.setSpacing(compact_spacing);
}

void configurePanelTree(QTreeWidget& tree, Qt::TextElideMode elide) {
  tree.setRootIsDecorated(false);
  tree.setUniformRowHeights(true);
  tree.setAllColumnsShowFocus(true);
  tree.setWordWrap(false);
  tree.setSelectionMode(QAbstractItemView::SingleSelection);
  tree.setSelectionBehavior(QAbstractItemView::SelectRows);
  tree.setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tree.setTextElideMode(elide);
  tree.setFocusPolicy(Qt::StrongFocus);
}

QLabel* makeEmptyState(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::NoTextInteraction);
  label->setProperty("state", "empty");
  return label;
}

}  // namespace wds::studio::theme
