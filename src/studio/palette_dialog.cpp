#include "palette_dialog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "theme/theme.hpp"

namespace wds::studio {
namespace {

QString palette_text(const char* string) {
  return QCoreApplication::translate("PaletteManager", string);
}

}  // namespace

PaletteDialog::PaletteDialog(QString directory, QWidget* parent)
    : QDialog(parent), directory_(std::move(directory)) {
  setWindowTitle(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Palette Plugins")));
  setMinimumWidth(475);
  auto* layout = new QVBoxLayout(this);
  theme::configurePanelLayout(*layout);
  status_ = new QLabel(this);
  color_ = new QLabel(this);
  color_->setObjectName("paletteColor");
  swatch_ = new QLabel(this);
  auto* preview = new QHBoxLayout;
  preview->addWidget(status_);
  preview->addStretch();
  preview->addWidget(swatch_);
  preview->addWidget(color_);
  layout->addLayout(preview);

  auto* notice = new QLabel(
      palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Choose a trusted native palette module.")),
      this);
  notice->setWordWrap(true);
  layout->addWidget(notice);
  auto* selection = new QHBoxLayout;
  source_ = new QLineEdit(this);
  source_->setObjectName("paletteSource");
  source_->setAccessibleName(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Module file")));
  browse_ = new QPushButton(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Browse…")), this);
  selection->addWidget(source_, 1);
  selection->addWidget(browse_);
  layout->addLayout(selection);

  error_ = new QLabel(this);
  error_->setObjectName("paletteError");
  error_->setTextFormat(Qt::PlainText);
  error_->setWordWrap(true);
  layout->addWidget(error_);
  auto* actions = new QHBoxLayout;
  remove_ = new QPushButton(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Remove")), this);
  remove_->setObjectName("paletteRemove");
  install_ = new QPushButton(this);
  install_->setObjectName("paletteInstall");
  close_ = new QPushButton(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Close")), this);
  actions->addWidget(remove_);
  actions->addStretch();
  actions->addWidget(install_);
  actions->addWidget(close_);
  layout->addLayout(actions);

  tool_ = new QProcess(this);
  auto executable = QCoreApplication::applicationDirPath() + "/wds_palette_tool";
#ifdef Q_OS_WIN
  executable += ".exe";
  tool_->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
  tool_->setProgram(executable);
  connect(tool_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      complete(
          palette_text(QT_TRANSLATE_NOOP("PaletteManager", "The plugin tool could not start.")));
  });
  auto* deadline = new QTimer(this);
  deadline->setSingleShot(true);
  connect(tool_, &QProcess::started, deadline, [deadline] { deadline->start(10000); });
  connect(deadline, &QTimer::timeout, tool_, &QProcess::kill);
  connect(tool_, &QProcess::finished, this, [this, deadline](int code, QProcess::ExitStatus exit) {
    deadline->stop();
    const auto output = QString::fromUtf8(tool_->readAllStandardOutput()).trimmed();
    const auto details = QString::fromUtf8(tool_->readAllStandardError()).trimmed();
    if (code != 0 || exit != QProcess::NormalExit) {
      if (details == "abi_mismatch") {
        complete(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Incompatible plugin version.")));
      } else {
        complete(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Plugin operation failed.")));
      }
      return;
    }
    if (tool_->arguments().value(0) != "--status") {
      run("--status");
      return;
    }
    const auto fields = output.split(u' ');
    bool valid = false;
    const auto rgb = fields.value(1).toUInt(&valid, 16);
    if (fields.size() != 2 || (fields[0] != "built-in" && fields[0] != "installed") || !valid ||
        rgb > 0xffffff) {
      complete(palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Invalid plugin status.")));
      return;
    }
    installed_ = fields[0] == "installed";
    status_->setText(
        installed_ ? palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Native module installed"))
                   : palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Built-in palette")));
    color_->setText('#' + QString::number(rgb, 16).rightJustified(6, QChar(u'0')));
    QImage sample(64, 22, QImage::Format_RGB32);
    sample.fill(0xff000000U | rgb);
    swatch_->setPixmap(QPixmap::fromImage(sample));
    complete({});
  });

  connect(source_, &QLineEdit::textChanged, this, &PaletteDialog::updateControls);
  connect(browse_, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Select Palette Module")), {},
        palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Modules (*.dll *.so *.dylib)")));
    if (!path.isEmpty()) source_->setText(path);
  });
  connect(install_, &QPushButton::clicked, this, [this] { run("--install", source_->text()); });
  connect(remove_, &QPushButton::clicked, this, [this] { run("--remove"); });
  connect(close_, &QPushButton::clicked, this, &PaletteDialog::reject);
  if (directory_.isEmpty() || !QDir().mkpath(directory_))
    complete(
        palette_text(QT_TRANSLATE_NOOP("PaletteManager", "The plugin folder is unavailable.")));
  else
    run("--status");
}

void PaletteDialog::reject() {
  if (!busy_) QDialog::reject();
}

void PaletteDialog::run(const QString& action, const QString& path) {
  busy_ = true;
  error_->clear();
  updateControls();
  QStringList arguments{action, directory_};
  if (!path.isEmpty()) arguments.append(QDir::cleanPath(path));
  tool_->start(tool_->program(), arguments);
}

void PaletteDialog::complete(const QString& error) {
  busy_ = false;
  error_->setText(error);
  updateControls();
}

void PaletteDialog::updateControls() {
  source_->setEnabled(!busy_);
  browse_->setEnabled(!busy_);
  install_->setEnabled(!busy_ && !source_->text().trimmed().isEmpty());
  install_->setText(installed_ ? palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Replace"))
                               : palette_text(QT_TRANSLATE_NOOP("PaletteManager", "Install")));
  remove_->setEnabled(!busy_ && installed_);
  close_->setEnabled(!busy_);
}

}  // namespace wds::studio
