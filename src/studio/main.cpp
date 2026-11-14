#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <optional>
#include <utility>

#include "bridge.hpp"
#include "platform_project.hpp"
#include "resource_panel.hpp"
#include "scene_tree.hpp"
#include "viewport.hpp"

namespace wds::studio {
namespace {

QString argument(qint64 number) { return QString::number(number); }

void applyTheme(QApplication& app) {
  app.setStyle(QStyleFactory::create("Fusion"));
  QPalette palette = app.palette();
  // Keep the native controls aligned with the roles in workbench.qss.
  palette.setColor(QPalette::Window, QColor("#1e1f22"));
  palette.setColor(QPalette::Base, QColor("#1a1b1e"));
  palette.setColor(QPalette::AlternateBase, QColor("#202226"));
  palette.setColor(QPalette::Button, QColor("#2a2c31"));
  palette.setColor(QPalette::Mid, QColor("#3a3d44"));
  palette.setColor(QPalette::Highlight, QColor("#2f5fa8"));
  palette.setColor(QPalette::Inactive, QPalette::Highlight, QColor("#3a4250"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::PlaceholderText, QColor("#a9adb5"));
  palette.setColor(QPalette::ToolTipBase, QColor("#26282c"));
  palette.setColor(QPalette::ToolTipText, QColor("#e6e7ea"));
  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    palette.setColor(role, QColor("#e6e7ea"));
    palette.setColor(QPalette::Disabled, role, QColor("#71767f"));
  }
  app.setPalette(palette);
  QFile theme(":/studio/theme/workbench.qss");
  if (theme.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(theme.readAll()));
}

QString diagnostic(const QJsonObject& result) {
  auto message = result.value("code").toString();
  const auto location = result.value("location").toString();
  const auto path = result.value("resource_path").toString();
  if (!location.isEmpty()) message += " · " + location;
  if (!path.isEmpty()) message += " · " + path;
  if (result.value("code") == "revision_conflict")
    message += QObject::tr(" · Reopen the project to load its current revision.");
  return message;
}

class Workbench final : public QMainWindow {
 public:
  Workbench() {
    setObjectName("workbench");
    setWindowTitle(tr("WDS Studio"));
    resize(1360, 840);
    setDockOptions(AllowTabbedDocks | AllowNestedDocks | AnimatedDocks);

    auto* project = menuBar()->addMenu(tr("&Project"));
    open_ = project->addAction(tr("&Open Project…"), this, &Workbench::chooseProject);
    open_->setShortcut(QKeySequence::Open);
    create_ = project->addAction(tr("&New Project…"), this, &Workbench::createProject);
    create_->setShortcut(QKeySequence::New);
    save_ = project->addAction(tr("&Save Property"), this, &Workbench::saveProperty);
    save_->setShortcut(QKeySequence::Save);
    revert_ = project->addAction(tr("Discard Property Draft"), this, &Workbench::discardProperty);
    close_ = project->addAction(tr("Close Project"), this, &Workbench::closeProject);
    auto* scene = menuBar()->addMenu(tr("&Scene"));
    play_ = scene->addAction(tr("&Play"), this, &Workbench::play);
    play_->setShortcut(QKeySequence(Qt::Key_F5));
    stop_ = scene->addAction(tr("S&top"), this, &Workbench::stop);
    stop_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    auto* view = menuBar()->addMenu(tr("&View"));

    auto* toolbar = addToolBar(tr("Workspace"));
    toolbar->setObjectName("workspaceToolbar");
    toolbar->setMovable(false);
    toolbar->addActions({open_, create_, save_});
    toolbar->addSeparator();
    toolbar->addActions({play_, stop_});
    toolbar->addSeparator();
    project_label_ = new QLabel(tr("No project open"), toolbar);
    toolbar->addWidget(project_label_);

    viewport_ = new Viewport([this](const QString& id) { selectEntity(id); }, this);
    setCentralWidget(viewport_);
    scene_tree_ = new SceneTreePanel([this](const QString& id) { selectEntity(id); }, this);
    auto* hierarchy = dock("sceneTree", tr("Scene"), Qt::LeftDockWidgetArea, scene_tree_, view);

    auto* property_panel = new QWidget(this);
    auto* form = new QFormLayout(property_panel);
    form->setContentsMargins(12, 14, 12, 14);
    entity_label_ = new QLabel(tr("Select an entity"), property_panel);
    form->addRow(entity_label_);
    const std::array labels{tr("Position X"), tr("Position Y"), tr("Velocity X"), tr("Velocity Y")};
    for (std::size_t index = 0; index < fields_.size(); ++index) {
      auto* value = new QSpinBox(property_panel);
      value->setAccessibleName(labels[index]);
      fields_[index] = value;
      form->addRow(labels[index], value);
      connect(value, &QSpinBox::valueChanged, this, [this, index](int) {
        if (!dirty_) editing_ = index;
        dirty_ = fields_[editing_]->value() != committed_[editing_];
        updateActions();
      });
    }
    draft_label_ = new QLabel(property_panel);
    draft_label_->setProperty("state", "modified");
    form->addRow(draft_label_);
    auto* inspector =
        dock("inspector", tr("Inspector"), Qt::RightDockWidgetArea, property_panel, view);

    resources_ = new ResourcePanel([this](const QString& id) { showResource(id); }, this);
    auto* assets = dock("resources", tr("Resources"), Qt::BottomDockWidgetArea, resources_, view);
    output_ = new QListWidget(this);
    auto* output = dock("output", tr("Output"), Qt::BottomDockWidgetArea, output_, view);
    tabifyDockWidget(assets, output);
    assets->raise();
    resizeDocks({hierarchy, inspector}, {225, 285}, Qt::Horizontal);
    resizeDocks({assets}, {190}, Qt::Vertical);

    revision_label_ = new QLabel(statusBar());
    statusBar()->addPermanentWidget(revision_label_);
    statusBar()->showMessage(tr("Open a WDS project to begin."));
    const QSettings settings;
    restoreGeometry(settings.value("workbench/geometry").toByteArray());
    restoreState(settings.value("workbench/layout").toByteArray(), 1);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, [this] {
      if (!session_->advance(1)) {
        timer_.stop();
        statusBar()->showMessage(tr("Playback reached its final tick. Select Stop to edit."));
        return;
      }
      drawSession();
    });
    updateActions();
  }

  void openProject(const QString& path) {
    if (busy_ || !canDiscard()) return;
    stop();
    root_ = QDir(path).absolutePath();
    clearProject();
    refresh();
  }

 protected:
  void closeEvent(QCloseEvent* event) override {
    if (busy_ || !canDiscard()) {
      event->ignore();
      return;
    }
    QSettings settings;
    settings.setValue("workbench/geometry", saveGeometry());
    settings.setValue("workbench/layout", saveState(1));
    QMainWindow::closeEvent(event);
  }

 private:
  QDockWidget* dock(const QString& id, const QString& title, Qt::DockWidgetArea area,
                    QWidget* panel, QMenu* menu) {
    auto* widget = new QDockWidget(title, this);
    widget->setObjectName(id);
    widget->setWidget(panel);
    addDockWidget(area, widget);
    menu->addAction(widget->toggleViewAction());
    return widget;
  }

  void chooseProject() {
    const auto path = QFileDialog::getExistingDirectory(this, tr("Open WDS Project"), root_);
    if (!path.isEmpty()) openProject(path);
  }

  void createProject() {
    const auto parent = QFileDialog::getExistingDirectory(this, tr("Choose Project Parent"), root_);
    if (parent.isEmpty()) return;
    bool accepted = false;
    const auto id = QInputDialog::getText(this, tr("New WDS Project"), tr("Project ID"),
                                          QLineEdit::Normal, "my-game", &accepted);
    if (!accepted || id.isEmpty() || !canDiscard()) return;
    if (id.contains('/') || id.contains('\\') || id == "." || id == "..") {
      report(tr("Use a project ID, such as my-game."));
      return;
    }
    const auto path = QDir(parent).filePath(id);
    setBusy(true);
    bridge_.run({"project", "create", path, id}, [this, path](const QJsonObject& result) {
      if (result.value("code") != "ok") {
        report(diagnostic(result));
        setBusy(false);
        return;
      }
      stop();
      root_ = path;
      clearProject();
      refresh();
    });
  }

  bool canDiscard() {
    return !dirty_ || QMessageBox::question(this, tr("Unsaved Property"),
                                            tr("Discard the unsaved property change?"),
                                            QMessageBox::Discard | QMessageBox::Cancel,
                                            QMessageBox::Cancel) == QMessageBox::Discard;
  }

  void closeProject() {
    if (!canDiscard()) return;
    stop();
    root_.clear();
    clearProject();
    statusBar()->showMessage(tr("Project closed."));
  }

  void clearProject() {
    project_id_.clear();
    revision_.reset();
    scene_ready_ = false;
    field_schema_ = {};
    entities_ = {};
    resources_model_ = {};
    selected_.clear();
    dirty_ = false;
    resources_->setResources(resources_model_);
    setEntities();
    populateInspector();
    updateActions();
  }

  void refresh() {
    setBusy(true);
    bridge_.run({"project", "resources", root_}, [this](const QJsonObject& result) {
      if (result.value("code") != "ok" || !result.value("revision").isDouble() ||
          !result.value("resources").isArray() || !result.value("project_id").isString()) {
        report(diagnostic(result));
        setBusy(false);
        return;
      }
      project_id_ = result.value("project_id").toString();
      revision_ = result.value("revision").toInteger();
      resources_model_ = result.value("resources").toArray();
      resources_->setResources(resources_model_);
      readScene();
    });
  }

  void readScene() {
    scene_ready_ = false;
    bridge_.run(
        {"project", "scene", root_, argument(*revision_)}, [this](const QJsonObject& result) {
          scene_ready_ =
              result.value("code") == "ok" && result.value("entities").isArray() &&
              result.value("fields").toArray().size() == static_cast<qsizetype>(fields_.size());
          entities_ = scene_ready_ ? result.value("entities").toArray() : QJsonArray{};
          field_schema_ = scene_ready_ ? result.value("fields").toArray() : QJsonArray{};
          setEntities();
          populateInspector();
          report(scene_ready_ ? tr("Project ready · %1").arg(root_) : diagnostic(result));
          setBusy(false);
        });
  }

  void setEntities() {
    scene_tree_->setEntities(entities_);
    scene_tree_->setSelectedId(selected_);
    viewport_->setScene(entities_, false);
    viewport_->setSelectedId(selected_);
  }

  void selectEntity(const QString& id) {
    if (id == selected_) return;
    if (busy_ || session_ || !canDiscard()) {
      scene_tree_->setSelectedId(selected_);
      return;
    }
    selected_ = id;
    dirty_ = false;
    scene_tree_->setSelectedId(selected_);
    viewport_->setSelectedId(id);
    populateInspector();
    updateActions();
  }

  void populateInspector() {
    QJsonObject entity;
    for (const auto item : entities_) {
      const auto candidate = item.toObject();
      if (candidate.value("id") == selected_) {
        entity = candidate;
        break;
      }
    }
    if (entity.isEmpty()) selected_.clear();
    entity_label_->setText(selected_.isEmpty() ? tr("Select an entity") : selected_);
    for (std::size_t index = 0; index < fields_.size(); ++index) {
      const QSignalBlocker block(fields_[index]);
      const auto spec = field_schema_.size() > static_cast<qsizetype>(index)
                            ? field_schema_.at(static_cast<qsizetype>(index)).toObject()
                            : QJsonObject{};
      fields_[index]->setRange(spec.value("minimum").toInt(), spec.value("maximum").toInt());
      committed_[index] = entity.value(spec.value("id").toString()).toInt();
      fields_[index]->setValue(committed_[index]);
    }
  }

  void showResource(const QString& id) {
    if (id.isEmpty()) return;
    for (const auto item : resources_model_) {
      const auto resource = item.toObject();
      if (resource.value("id") == id) statusBar()->showMessage(resource.value("path").toString());
    }
  }

  void discardProperty() {
    dirty_ = false;
    populateInspector();
    updateActions();
  }

  void saveProperty() {
    if (!dirty_ || busy_ || !revision_) return;
    const auto field = field_schema_.at(static_cast<qsizetype>(editing_)).toObject().value("id");
    const auto value = fields_[editing_]->value();
    QStringList args{"project",        "preview-scene-field", root_,          selected_,
                     field.toString(), argument(*revision_),  argument(value)};
    setBusy(true);
    bridge_.run(args, [this, args](const QJsonObject& preview) mutable {
      if (preview.value("code") != "ok") {
        report(diagnostic(preview));
        setBusy(false);
        return;
      }
      args[1] = "set-scene-field";
      bridge_.run(args, [this](const QJsonObject& result) {
        if (result.value("code") != "ok" || !result.value("revision").isDouble()) {
          report(diagnostic(result));
          setBusy(false);
          return;
        }
        revision_ = result.value("revision").toInteger();
        dirty_ = false;
        report(tr("Saved %1 · %2 → %3")
                   .arg(result.value("location").toString(),
                        argument(result.value("old_value").toInteger()),
                        argument(result.value("new_value").toInteger())));
        readScene();
      });
    });
  }

  void play() {
    if (busy_ || dirty_ || !revision_ || !scene_ready_) return;
#ifdef Q_OS_WIN
    const std::filesystem::path path(root_.toStdWString());
#else
    const std::filesystem::path path(root_.toStdString());
#endif
    auto result = platform::start_scene(path, static_cast<std::uint64_t>(*revision_));
    if (!result.session) {
      report(QString::fromStdString(result.code + " · " + result.location));
      return;
    }
    session_ = std::move(result.session);
    drawSession();
    timer_.start();
    updateActions();
  }

  void drawSession() {
    QJsonArray entities;
    for (const auto& entity : session_->snapshot().entities) {
      entities.append(QJsonObject{{"id", QString::fromStdString(entity.id)},
                                  {"x", static_cast<qint64>(entity.x)},
                                  {"y", static_cast<qint64>(entity.y)}});
    }
    viewport_->setScene(std::move(entities), true);
    statusBar()->showMessage(tr("Playing · tick %1").arg(session_->snapshot().ticks));
  }

  void stop() {
    if (!session_) return;
    timer_.stop();
    session_.reset();
    viewport_->setScene(entities_, false);
    statusBar()->showMessage(tr("Playback stopped."));
    updateActions();
  }

  void report(const QString& message) {
    if (output_->count() == 200) delete output_->takeItem(0);
    output_->addItem(message);
    output_->scrollToBottom();
    statusBar()->showMessage(message);
  }

  void setBusy(bool busy) {
    busy_ = busy;
    updateActions();
  }

  void updateActions() {
    const bool editable = scene_ready_ && !selected_.isEmpty() && !busy_ && !session_;
    for (std::size_t index = 0; index < fields_.size(); ++index)
      fields_[index]->setEnabled(editable && (!dirty_ || index == editing_));
    scene_tree_->setEnabled(!busy_ && !session_);
    open_->setEnabled(!busy_);
    create_->setEnabled(!busy_);
    close_->setEnabled(!busy_ && !root_.isEmpty());
    save_->setEnabled(editable && dirty_);
    revert_->setEnabled(editable && dirty_);
    play_->setEnabled(scene_ready_ && !busy_ && !session_ && !dirty_ && revision_.has_value());
    stop_->setEnabled(session_.has_value());
    project_label_->setText(project_id_.isEmpty() ? tr("No project open") : project_id_);
    const auto revision = revision_ ? tr("Revision %1").arg(*revision_) : QString{};
    revision_label_->setText(revision + (dirty_ ? tr(" · Unsaved") : QString{}));
    draft_label_->setText(
        dirty_ ? tr("Unsaved · %1 → %2").arg(committed_[editing_]).arg(fields_[editing_]->value())
               : QString{});
    draft_label_->setVisible(dirty_);
    setWindowTitle(project_id_.isEmpty()
                       ? tr("WDS Studio")
                       : tr("%1%2 — WDS Studio").arg(project_id_, dirty_ ? "*" : ""));
  }

  ProjectBridge bridge_{this};
  QTimer timer_;
  Viewport* viewport_;
  SceneTreePanel* scene_tree_;
  ResourcePanel* resources_;
  QListWidget* output_;
  QLabel* project_label_;
  QLabel* revision_label_;
  QLabel* entity_label_;
  QLabel* draft_label_;
  QAction* open_;
  QAction* create_;
  QAction* close_;
  QAction* save_;
  QAction* revert_;
  QAction* play_;
  QAction* stop_;
  std::array<QSpinBox*, platform::scene_fields.size()> fields_{};
  std::array<int, platform::scene_fields.size()> committed_{};
  QString root_;
  QString project_id_;
  QString selected_;
  std::optional<qint64> revision_;
  std::optional<platform::SceneSession> session_;
  QJsonArray entities_;
  QJsonArray resources_model_;
  QJsonArray field_schema_;
  bool busy_ = false;
  bool dirty_ = false;
  bool scene_ready_ = false;
  std::size_t editing_ = 0;
};

}  // namespace
}  // namespace wds::studio

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("WDS");
  QCoreApplication::setApplicationName("WDS Studio");
  wds::studio::applyTheme(app);
  wds::studio::Workbench window;
  window.show();
  if (app.arguments().size() == 2) window.openProject(app.arguments().at(1));
  return app.exec();
}
