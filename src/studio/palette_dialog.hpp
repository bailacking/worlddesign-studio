#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;

namespace wds::studio {

class PaletteDialog final : public QDialog {
 public:
  PaletteDialog(QString directory, QWidget* parent);
  void reject() override;

 private:
  void run(const QString& action, const QString& path = {});
  void complete(const QString& error);
  void updateControls();

  QString directory_;
  bool installed_ = false;
  bool busy_ = false;
  QLineEdit* source_;
  QLabel* status_;
  QLabel* color_;
  QLabel* swatch_;
  QLabel* error_;
  QPushButton* browse_;
  QPushButton* install_;
  QPushButton* remove_;
  QPushButton* close_;
  QProcess* tool_;
};

}  // namespace wds::studio
