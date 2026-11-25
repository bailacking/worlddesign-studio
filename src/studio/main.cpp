#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <deque>
#include <optional>
#include <utility>

#include "asset_preview.hpp"
#include "bridge.hpp"
#include "i18n/language.hpp"
#include "output_panel.hpp"
#include "platform_project.hpp"
#include "resource_panel.hpp"
#include "scene_tree.hpp"
#include "theme/theme.hpp"
#include "viewport.hpp"
#include "voxel_viewport.hpp"
#include "workshop_dialog.hpp"

namespace wds::studio {
namespace {

QString argument(qint64 number) { return QString::number(number); }

QString file_kind(const QString& path) {
  const auto extension = QFileInfo(path).suffix().toLower();
  if (extension == "png") return "image";
  if (extension == "txt" || extension == "md") return "text";
  if (extension == "json") return "data";
  return {};
}

QString dropped_file(const QMimeData* mime) {
  if (!mime->hasUrls()) return {};
  const auto urls = mime->urls();
  if (urls.size() != 1 || !urls.front().isLocalFile()) return {};
  const auto path = urls.front().toLocalFile();
  return file_kind(path).isEmpty() ? QString{} : path;
}

QString diagnostic(const QJsonObject& result) {
  static constexpr std::array codes{
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "already_exists"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "cli_invalid_result"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "cli_timeout"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "cli_unavailable"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "cleanup_incomplete"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "duplicate_entity_id"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "duplicate_id"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "duplicate_path"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "edit_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "export_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "image_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "import_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_argument"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_content"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_entity"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_entry_scene"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_export_destination"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_field"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_manifest"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_mode"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_path"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_png"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_sprite"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_provenance"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_resolution"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_resource"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_runtime_dependency"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_source"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_utf8"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_scene"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_voxel_materials"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "invalid_voxel_world"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "io_error"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "merge_input_changed"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "missing_file"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "missing_manifest"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "missing_reference"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "missing_runtime_dependency"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "permission_denied"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "recovery_required"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "revision_conflict"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "revision_exhausted"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "scene_content_conflict"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "scene_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "source_changed"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "source_too_large"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "target_exists"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsafe_path"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsupported_export_target"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsupported_operation"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsupported_scene_kind"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsupported_scene_version"),
      QT_TRANSLATE_NOOP("ProjectDiagnostic", "unsupported_version")};
  const auto code = result.value("code").toString();
  const auto found = std::ranges::find_if(codes, [&](const char* key) { return code == key; });
  auto message =
      found == codes.end() ? code : QCoreApplication::translate("ProjectDiagnostic", *found);
  if (message != code) message += " (" + code + ")";
  const auto location = result.value("location").toString();
  const auto path = result.value("resource_path").toString();
  if (!location.isEmpty()) message += " · " + location;
  if (!path.isEmpty()) message += " · " + path;
  if (code == "revision_conflict")
    message += QCoreApplication::translate("ProjectDiagnostic",
                                           " · Reopen the project to load its current revision.");
  return message;
}

class Workbench final : public QMainWindow {
  Q_OBJECT

 private:
  struct Message {
    const char* pattern = nullptr;
    QStringList arguments;
    QJsonObject error;
    QString level = "info";

    QString severity() const {
      if (error.isEmpty()) return level;
      const auto code = error.value("code").toString();
      return (code == "revision_conflict" || code == "scene_content_conflict") ? "warning"
                                                                               : "error";
    }

    QString text() const {
      if (!error.isEmpty()) return diagnostic(error);
      if (!pattern) return arguments.value(0);
      const auto translated = tr(pattern);
      if (arguments.size() == 3) return translated.arg(arguments[0], arguments[1], arguments[2]);
      return arguments.isEmpty() ? translated : translated.arg(arguments[0]);
    }
  };

  struct HistoryEntry {
    QJsonObject change;
    QString before_selection;
    QString after_selection;
  };

 public:
  explicit Workbench(LanguageSettings& language) : language_(language) {
    setObjectName("workbench");
    setAcceptDrops(true);
    resize(1360, 840);
    setDockOptions(AllowTabbedDocks | AllowNestedDocks | AnimatedDocks);

    project_menu_ = menuBar()->addMenu(QString{});
    open_ = project_menu_->addAction(QString{}, this, &Workbench::chooseProject);
    open_->setShortcut(QKeySequence::Open);
    reload_ = project_menu_->addAction(QString{}, this, [this] { openProject(root_); });
    create_ = project_menu_->addAction(QString{}, this, &Workbench::createProject);
    create_->setShortcut(QKeySequence::New);
    save_ = project_menu_->addAction(QString{}, this, &Workbench::saveProperty);
    save_->setShortcut(QKeySequence::Save);
    revert_ = project_menu_->addAction(QString{}, this, &Workbench::discardProperty);
    import_ = project_menu_->addAction(QString{}, this, &Workbench::importResource);
    merge_ = project_menu_->addAction(QString{}, this, &Workbench::mergeProjectResources);
    export_ = project_menu_->addAction(QString{}, this, &Workbench::exportGame);
#ifndef Q_OS_WIN
    export_->setVisible(false);
#endif
    clear_cache_ = project_menu_->addAction(QString{}, this, &Workbench::clearThumbnailCache);
    recover_ = project_menu_->addAction(QString{}, this, &Workbench::recoverProject);
    operations_ = project_menu_->addAction(QString{}, this, &Workbench::showOperations);
    close_ = project_menu_->addAction(QString{}, this, &Workbench::closeProject);
    edit_menu_ = menuBar()->addMenu(QString{});
    undo_ = edit_menu_->addAction(QString{}, this, [this] { restoreHistory(false); });
    redo_ = edit_menu_->addAction(QString{}, this, [this] { restoreHistory(true); });
    edit_menu_->addSeparator();
    edit_data_ = edit_menu_->addAction(QString{}, this, &Workbench::editDataField);
    undo_->setShortcuts(QKeySequence::Undo);
    redo_->setShortcuts(QKeySequence::Redo);
    scene_menu_ = menuBar()->addMenu(QString{});
    add_entity_ = scene_menu_->addAction(QString{}, this, [this] { nameEntity(false); });
    copy_entity_ = scene_menu_->addAction(QString{}, this, [this] { nameEntity(true); });
    remove_entity_ = scene_menu_->addAction(QString{}, this, &Workbench::removeEntity);
    move_up_ = scene_menu_->addAction(QString{}, this, [this] { moveEntity(-1); });
    move_down_ = scene_menu_->addAction(QString{}, this, [this] { moveEntity(1); });
    scene_menu_->addSeparator();
    play_ = scene_menu_->addAction(QString{}, this, &Workbench::play);
    play_->setShortcut(QKeySequence(Qt::Key_F5));
    stop_ = scene_menu_->addAction(QString{}, this, &Workbench::stop);
    stop_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    workshop_ = scene_menu_->addAction(QString{}, this, &Workbench::openWorkshop);
    workshop_->setShortcut(QKeySequence(Qt::Key_F6));
    recipes_ = scene_menu_->addAction(QString{}, this, &Workbench::openRecipeGraph);
    recipes_->setObjectName("expeditionRecipes");
    palette_ = scene_menu_->addAction(QString{}, this, &Workbench::changePalette);
    view_menu_ = menuBar()->addMenu(QString{});
    settings_menu_ = menuBar()->addMenu(QString{});
    settings_action_ = settings_menu_->addAction(QString{}, this, &Workbench::showSettings);

    toolbar_ = addToolBar(QString{});
    toolbar_->setObjectName("workspaceToolbar");
    toolbar_->setMovable(false);
    toolbar_->addActions({open_, create_, save_});
    toolbar_->addSeparator();
    toolbar_->addActions({undo_, redo_});
    toolbar_->addSeparator();
    toolbar_->addActions({play_, stop_, workshop_});
    toolbar_->addSeparator();
    project_label_ = new QLabel(toolbar_);
    project_label_->setTextFormat(Qt::PlainText);
    toolbar_->addWidget(project_label_);

    viewport_ = new Viewport([this](const QString& id) { selectEntity(id); }, this);
    voxel_view_ = new VoxelViewport(
        [this] {
          if (!voxel_preview_) voxel_dirty_ = true;
          updateActions();
        },
        this);
    views_ = new QStackedWidget(this);
    views_->addWidget(viewport_);
    views_->addWidget(voxel_view_);
    setCentralWidget(views_);
    scene_tree_ = new SceneTreePanel([this](const QString& id) { selectEntity(id); }, this);
    hierarchy_ = dock("sceneTree", Qt::LeftDockWidgetArea, scene_tree_);

    auto* property_panel = new QWidget(this);
    auto* form = new QFormLayout(property_panel);
    form->setContentsMargins(12, 14, 12, 14);
    entity_label_ = new QLabel(property_panel);
    form->addRow(entity_label_);
    for (std::size_t index = 0; index < fields_.size(); ++index) {
      auto* value = new QSpinBox(property_panel);
      fields_[index] = value;
      field_labels_[index] = new QLabel(property_panel);
      field_labels_[index]->setBuddy(value);
      form->addRow(field_labels_[index], value);
      connect(value, &QSpinBox::valueChanged, this, [this, index](int) {
        if (!dirty_) editing_ = index;
        dirty_ = fields_[editing_]->value() != committed_[editing_];
        updateActions();
      });
    }
    draft_label_ = new QLabel(property_panel);
    draft_label_->setProperty("state", "modified");
    form->addRow(draft_label_);
    inspector_ = dock("inspector", Qt::RightDockWidgetArea, property_panel);

    resources_ = new ResourcePanel([this](const QString& id) { showResource(id); }, this);
    assets_ = dock("resources", Qt::BottomDockWidgetArea, resources_);
    output_ = new OutputPanel(this);
    output_dock_ = dock("output", Qt::BottomDockWidgetArea, output_);
    tabifyDockWidget(assets_, output_dock_);
    assets_->raise();
    resizeDocks({hierarchy_, inspector_}, {225, 285}, Qt::Horizontal);
    resizeDocks({assets_}, {190}, Qt::Vertical);

    revision_label_ = new QLabel(statusBar());
    statusBar()->addPermanentWidget(revision_label_);
    setStatus(QT_TR_NOOP("Open a WDS project to begin."));
    const QSettings settings;
    restoreGeometry(settings.value("workbench/geometry").toByteArray());
    restoreState(settings.value("workbench/layout").toByteArray(), 1);
    timer_.setInterval(16);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &Workbench::advancePlayback);
    retranslate();
  }

  void openProject(const QString& path) {
    if (busy_ || !canDiscard()) return;
    stop();
    root_ = QDir(path).absolutePath();
    clearProject();
    refresh();
  }

  bool startEditSmoke() {
    if (!scene_ready_ || project_id_ != "scene-editing" || revision_ != 0) return false;
    selectEntity("drifter");
    for (std::size_t index = 0; index < fields_.size(); ++index) {
      if (field_schema_.at(static_cast<qsizetype>(index)).toObject().value("id") != "vx") continue;
      if (selectedIndex() < 0 || !fields_[index]->isEnabled() || committed_[index] != 8)
        return false;
      fields_[index]->setValue(31);
      save_->trigger();
      return busy_;
    }
    return false;
  }

  bool editSmokeSaved() const {
    const auto index = selectedIndex();
    return scene_ready_ && revision_ == 1 && index >= 0 &&
           entities_.at(index).toObject().value("vx").toInt() == 31;
  }

 signals:
  void inspectionFinished(bool ready);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override {
    if (!import_->isEnabled() || !event->possibleActions().testFlag(Qt::CopyAction) ||
        dropped_file(event->mimeData()).isEmpty())
      return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
  }

  void dropEvent(QDropEvent* event) override {
    const auto path = dropped_file(event->mimeData());
    if (!import_->isEnabled() || !event->possibleActions().testFlag(Qt::CopyAction) ||
        path.isEmpty())
      return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    importFile(path);
  }

  void changeEvent(QEvent* event) override {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
      viewport_->clearSceneInput();
      voxel_view_->clearInput();
    }
  }

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
  QDockWidget* dock(const QString& id, Qt::DockWidgetArea area, QWidget* panel) {
    auto* widget = new QDockWidget(this);
    widget->setObjectName(id);
    widget->setWidget(panel);
    addDockWidget(area, widget);
    view_menu_->addAction(widget->toggleViewAction());
    return widget;
  }

  void retranslate() {
    project_menu_->setTitle(tr("&Project"));
    edit_menu_->setTitle(tr("&Edit"));
    undo_->setText(tr("&Undo Change"));
    redo_->setText(tr("&Redo Change"));
    scene_menu_->setTitle(tr("&Scene"));
    view_menu_->setTitle(tr("&View"));
    settings_menu_->setTitle(tr("Se&ttings"));
    settings_action_->setText(tr("Interface Language…"));
    open_->setText(tr("&Open Project…"));
    reload_->setText(tr("Reload Project"));
    create_->setText(tr("&New Project…"));
    save_->setText(tr("&Save Property"));
    revert_->setText(tr("Discard Property Draft"));
    import_->setText(tr("Import Resource…"));
    merge_->setText(tr("Merge Resources…"));
    export_->setText(tr("Export for Windows x64…"));
    clear_cache_->setText(tr("Clear Thumbnail Cache"));
    recover_->setText(tr("Recover Project…"));
    operations_->setText(tr("Available Operations…"));
    close_->setText(tr("Close Project"));
    edit_data_->setText(tr("Edit Integer Data Field…"));
    add_entity_->setText(tr("Add Entity…"));
    copy_entity_->setText(tr("Duplicate Entity…"));
    remove_entity_->setText(tr("Delete Entity…"));
    move_up_->setText(tr("Move Entity Up"));
    move_down_->setText(tr("Move Entity Down"));
    play_->setText(tr("&Play"));
    workshop_->setText(QCoreApplication::translate("WorkshopDialog", "Items and recipes"));
    recipes_->setText(tr("Crafting Dependencies…"));
    palette_->setText(tr("Voxel Palette…"));
    stop_->setText(tr("S&top"));
    toolbar_->setWindowTitle(tr("Workspace"));
    hierarchy_->setWindowTitle(tr("Scene"));
    inspector_->setWindowTitle(tr("Inspector"));
    assets_->setWindowTitle(tr("Resources"));
    output_dock_->setWindowTitle(tr("Output"));
    output_->setAccessibleName(tr("Output"));
    const std::array labels{tr("Position X"), tr("Position Y"), tr("Velocity X"), tr("Velocity Y")};
    for (std::size_t index = 0; index < fields_.size(); ++index) {
      field_labels_[index]->setText(labels[index]);
      fields_[index]->setAccessibleName(labels[index]);
    }
    entity_label_->setText(selected_.isEmpty() ? tr("Select an entity") : selected_);
    updateOutput();
    statusBar()->showMessage(status_.text());
    updateActions();
  }

  void showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Settings"));
    auto* form = new QFormLayout(&dialog);
    auto* languages = new QComboBox(&dialog);
    languages->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    languages->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languages->setCurrentIndex(languages->findData(language_.language()));
    languages->setAccessibleName(tr("Interface language"));
    form->addRow(tr("Interface language"), languages);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted &&
        !language_.setLanguage(languages->currentData().toString()))
      QMessageBox::warning(this, tr("Settings"), tr("Could not apply the interface language."));
  }

  void showOperations() {
    setBusy(true);
    bridge_.discover([this](const QJsonObject& result) {
      setBusy(false);
      if (result.value("code") != "ok" || !result.value("operations").isArray()) {
        report({nullptr, {}, result});
        return;
      }
      QDialog dialog(this);
      dialog.setWindowTitle(tr("Available Operations"));
      dialog.resize(620, 420);
      QVBoxLayout layout(&dialog);
      QLabel usage(tr("This window · %1 requests · %2 ms")
                       .arg(bridge_.requests())
                       .arg(bridge_.elapsedMs(), 0, 'f', 1),
                   &dialog);
      QPlainTextEdit list(&dialog);
      list.setReadOnly(true);
      list.setAccessibleName(tr("Available Operations"));
      list.setPlainText(QString::fromUtf8(
          QJsonDocument(result.value("operations").toArray()).toJson(QJsonDocument::Indented)));
      QDialogButtonBox buttons(QDialogButtonBox::Close, &dialog);
      buttons.button(QDialogButtonBox::Close)->setText(tr("Close"));
      connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
      layout.addWidget(&usage);
      layout.addWidget(&list);
      layout.addWidget(&buttons);
      dialog.exec();
    });
  }

  void chooseProject() {
    const auto path = QFileDialog::getExistingDirectory(
        this, tr("Open WDS Project"), root_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) openProject(path);
  }

  void createProject() {
    const auto parent = QFileDialog::getExistingDirectory(
        this, tr("Choose Project Parent"), root_,
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (parent.isEmpty()) return;
    bool accepted = false;
    const auto id = QInputDialog::getText(this, tr("New WDS Project"), tr("Project ID"),
                                          QLineEdit::Normal, "my-game", &accepted);
    if (!accepted || id.isEmpty()) return;
    if (id.contains('/') || id.contains('\\') || id == "." || id == "..") {
      report({QT_TR_NOOP("Use a project ID, such as my-game."), {}, {}, "warning"});
      return;
    }
    const QStringList kinds{tr("2D Scene"), tr("3D Voxel World")};
    const auto type = QInputDialog::getItem(this, tr("New WDS Project"), tr("Project Type"), kinds,
                                            0, false, &accepted);
    if (!accepted || !canDiscard()) return;
    const auto path = QDir(parent).filePath(id);
    setBusy(true);
    bridge_.request(path, "project.create", "apply", {},
                    {{"project_id", id}, {"kind", type == kinds.at(1) ? "voxel" : "scene"}},
                    [this, path](const QJsonObject& result) {
                      if (result.value("code") != "ok") {
                        report({nullptr, {}, result});
                        setBusy(false);
                        return;
                      }
                      stop();
                      root_ = path;
                      clearProject();
                      refresh();
                    });
  }

  void importResource() {
    if (busy_ || playing() || hasDraft() || !revision_) return;
    const auto source =
        QFileDialog::getOpenFileName(this, tr("Choose Resource File"), {},
                                     tr("Supported files (*.json *.png *.txt *.md);;All files (*)"),
                                     nullptr, QFileDialog::DontUseNativeDialog);
    if (!source.isEmpty()) importFile(source);
  }

  void mergeProjectResources() {
    if (busy_ || playing() || hasDraft() || !revision_ || resources_model_.isEmpty()) return;
    const auto parent = QFileInfo(root_).absolutePath();
    const auto options = QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog;
    const auto base =
        QFileDialog::getExistingDirectory(this, tr("Choose Common Ancestor"), parent, options);
    if (base.isEmpty()) return;
    const auto incoming =
        QFileDialog::getExistingDirectory(this, tr("Choose Incoming Project"), parent, options);
    if (incoming.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Merge Resources"));
    dialog.resize(460, 360);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Select up to eight resources to compare."), &dialog));
    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto row : resources_model_) {
      const auto resource = row.toObject();
      const auto id = resource.value("id").toString();
      auto* item = new QListWidgetItem(
          QStringLiteral("%1  ·  %2").arg(id, resource.value("path").toString()), list);
      item->setData(Qt::UserRole, id);
    }
    layout->addWidget(list);
    auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* preview = actions->button(QDialogButtonBox::Ok);
    preview->setText(tr("Preview Merge"));
    preview->setEnabled(false);
    connect(list, &QListWidget::itemSelectionChanged, &dialog, [list, preview] {
      const auto count = list->selectedItems().size();
      preview->setEnabled(count > 0 && count <= 8);
    });
    connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(actions);
    if (dialog.exec() != QDialog::Accepted) return;
    QJsonArray selected;
    for (const auto* item : list->selectedItems())
      selected.append(item->data(Qt::UserRole).toString());
    setBusy(true);
    bridge_.request(
        base, "project.open", "read", {}, {},
        [this, base, incoming, selected](const QJsonObject& ancestor) {
          if (!mergeSourceReady(ancestor)) return;
          const auto base_revision = ancestor.value("revision");
          bridge_.request(
              incoming, "project.open", "read", {}, {},
              [this, base, incoming, selected, base_revision](const QJsonObject& other) {
                if (!mergeSourceReady(other)) return;
                previewResourceMerge({{"base_directory", base},
                                      {"base_revision", base_revision},
                                      {"other_directory", incoming},
                                      {"other_revision", other.value("revision")},
                                      {"resources", selected}});
              });
        });
  }

  bool mergeSourceReady(const QJsonObject& result) {
    if (result.value("code") == "ok" && result.value("revision").isDouble() &&
        result.value("project_id") == project_id_)
      return true;
    if (result.value("code") == "ok")
      setStatus(QT_TR_NOOP("Choose a checkout of the current project."));
    else
      report({nullptr, {}, result});
    setBusy(false);
    return false;
  }

  void previewResourceMerge(const QJsonObject& input) {
    bridge_.request(root_, "project.merge", "preview", revision_, input,
                    [this, input](const QJsonObject& result) {
                      if (result.value("code") == "merge_conflict") {
                        showMergeConflicts(input, result);
                        return;
                      }
                      if (result.value("code") != "ok") {
                        report({nullptr, {}, result});
                        setBusy(false);
                        return;
                      }
                      if (!result.value("changed").toBool()) {
                        setStatus(QT_TR_NOOP("The selected resources already agree."));
                        setBusy(false);
                        return;
                      }
                      QStringList names;
                      for (const auto id : result.value("resources").toArray())
                        names.append(id.toString());
                      if (QMessageBox::question(
                              this, tr("Merge Resources"),
                              tr("Commit changes to these resources?\n%1").arg(names.join(u'\n')),
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes) {
                        cancelPreview("project.merge", input);
                        return;
                      }
                      bridge_.request(root_, "project.merge", "apply", revision_, input,
                                      [this](const QJsonObject& applied) {
                                        if (applied.value("code") != "ok") {
                                          report({nullptr, {}, applied});
                                          setBusy(false);
                                          return;
                                        }
                                        recordHistory(applied, selected_);
                                        revision_ = applied.value("revision").toInteger();
                                        setStatus(QT_TR_NOOP("Resource merge committed."));
                                        refresh(false);
                                      });
                    });
  }

  bool chooseMergeVersions(QJsonObject& input, const QJsonObject& result) {
    const auto versions = result.value("versions").toArray();
    if (versions.isEmpty() || !result.value("merge_token").isString()) return false;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Choose Conflict Versions"));
    dialog.resize(830, 380);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Choose a version for each conflict."), &dialog));
    auto* table = new QTableWidget(static_cast<int>(versions.size()), 5, &dialog);
    table->setHorizontalHeaderLabels(
        {tr("Resource"), tr("Field"), tr("Current Version"), tr("Incoming Version"), tr("Choice")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table);
    auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* preview = actions->button(QDialogButtonBox::Ok);
    preview->setText(tr("Preview Merge"));
    preview->setEnabled(false);
    std::vector<QComboBox*> choices;
    int row = 0;
    for (const auto entry : versions) {
      const auto version = entry.toObject();
      const auto summary = [&version](const char* side) {
        const auto value = version.value(side);
        return value.isNull() ? tr("Deleted") : value.toString();
      };
      const QStringList columns{version.value("resource").toString(),
                                version.value("path").toString(), summary("current"),
                                summary("incoming")};
      for (int column = 0; column < columns.size(); ++column) {
        auto* cell = new QTableWidgetItem(columns.at(column));
        cell->setToolTip(cell->text().toHtmlEscaped());
        table->setItem(row, column, cell);
      }
      auto* select = new QComboBox(table);
      select->addItem(tr("Select version"));
      select->addItem(tr("Current Version"), "current");
      select->addItem(tr("Incoming Version"), "incoming");
      choices.push_back(select);
      table->setCellWidget(row++, 4, select);
      connect(select, &QComboBox::currentIndexChanged, &dialog, [&choices, preview] {
        preview->setEnabled(std::ranges::all_of(
            choices, [](const QComboBox* choice) { return choice->currentIndex() > 0; }));
      });
    }
    connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(actions);
    if (dialog.exec() != QDialog::Accepted) return false;
    auto resolutions = input.value("resolutions").toArray();
    for (qsizetype index = 0; index < versions.size(); ++index) {
      const auto version = versions.at(index).toObject();
      resolutions.append(QJsonObject{
          {"resource", version.value("resource")},
          {"path", version.value("path")},
          {"side", choices[static_cast<std::size_t>(index)]->currentData().toString()}});
    }
    input["resolutions"] = resolutions;
    input["merge_token"] = result.value("merge_token");
    return true;
  }

  void showMergeConflicts(QJsonObject input, const QJsonObject& result) {
    QStringList ids, paths;
    for (const auto conflict : result.value("conflicts").toArray()) {
      const auto entry = conflict.toObject();
      const auto id = entry.value("resource").toString();
      ids.append(id);
      paths.append(id + entry.value("path").toString());
    }
    QJsonArray selected;
    for (const auto id : input.value("resources").toArray())
      if (!ids.contains(id.toString())) selected.append(id);
    QMessageBox summary(QMessageBox::Warning, tr("Resource Conflicts"),
                        tr("The listed resources contain changes on both sides."),
                        QMessageBox::Cancel, this);
    summary.setDetailedText(paths.join(u'\n'));
    auto* choose = summary.addButton(tr("Choose Conflict Versions"), QMessageBox::ActionRole);
    QPushButton* continue_merge = nullptr;
    if (!selected.isEmpty())
      continue_merge =
          summary.addButton(tr("Keep Conflicting Resources and Continue"), QMessageBox::AcceptRole);
    summary.exec();
    if (summary.clickedButton() == choose && chooseMergeVersions(input, result)) {
      previewResourceMerge(input);
    } else if (continue_merge && summary.clickedButton() == continue_merge) {
      input["resources"] = selected;
      input.remove("resolutions");
      input.remove("merge_token");
      previewResourceMerge(input);
    } else {
      cancelPreview("project.merge", input);
    }
  }

  void exportGame() {
    if (busy_ || playing() || hasDraft() || !revision_ || (!scene_ready_ && !voxel_scene_)) return;
    const auto parent = QFileDialog::getExistingDirectory(
        this, tr("Choose Export Parent"), QFileInfo(root_).absolutePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (parent.isEmpty()) return;

    bool accepted = false;
    const auto name =
        QInputDialog::getText(this, tr("Export for Windows x64"),
                              tr("New folder for revision %1").arg(*revision_), QLineEdit::Normal,
                              project_id_ + "-win64-r" + argument(*revision_), &accepted)
            .trimmed();
    if (!accepted) return;
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') ||
        name == "." || name == "..") {
      report({QT_TR_NOOP("Enter a name for a new export folder."), {}, {}, "warning"});
      return;
    }

    const auto destination = QDir(parent).filePath(name);
    setBusy(true);
    setStatus(QT_TR_NOOP("Exporting revision %1…"), {argument(*revision_)});
    bridge_.request(
        root_, "project.export", "apply", revision_,
        {{"target", "windows-x64"}, {"destination", destination}},
        [this](const QJsonObject& result) {
          setBusy(false);
          if (result.value("code") != "ok") {
            report({nullptr, {}, result});
            return;
          }
          const auto path = result.value("destination").toString();
          report({QT_TR_NOOP("Exported revision %1 · %2"),
                  {argument(result.value("revision").toInteger()), QDir::toNativeSeparators(path)},
                  {},
                  "info"});
          QMessageBox notice(QMessageBox::Information, tr("Export Complete"),
                             tr("Open Play.exe from the exported folder to run the game."),
                             QMessageBox::Close, this);
          auto* show = notice.addButton(tr("Open Folder"), QMessageBox::ActionRole);
          notice.exec();
          if (notice.clickedButton() == show) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
  }

  void importFile(const QString& source) {
    if (busy_ || playing() || hasDraft() || !revision_) return;
    const auto suggested_kind = file_kind(source);
    auto suggested_id = QFileInfo(source).completeBaseName().toLower();
    suggested_id.replace(QRegularExpression("[^a-z0-9]+"), "-");
    suggested_id.remove(QRegularExpression("^-|-$"));
    if (suggested_id.isEmpty())
      suggested_id = suggested_kind == "image" || suggested_kind == "text" ? suggested_kind
                                                                           : QString("resource");
    else if (suggested_id.front().isDigit())
      suggested_id = "resource-" + suggested_id;
    suggested_id = suggested_id.left(59);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Import Resource…"));
    QFormLayout form(&dialog);
    QLineEdit id(suggested_id, &dialog);
    QComboBox kind(&dialog);
    kind.addItem(tr("Data"), "data");
    kind.addItem(tr("Scene"), "scene");
    kind.addItem(tr("PNG Image"), "image");
    kind.addItem(tr("UTF-8 Text"), "text");
    const int type_index = kind.findData(suggested_kind);
    if (type_index >= 0) kind.setCurrentIndex(type_index);
    const auto targetFor = [&kind](const QString& id) {
      const auto chosen = kind.currentData().toString();
      const auto suffix = chosen == "image" ? "png" : chosen == "text" ? "txt" : "json";
      return QString("assets/%1.%2").arg(id, suffix);
    };
    QLineEdit target(targetFor(suggested_id), &dialog);
    target.setPlaceholderText(QStringLiteral("assets/example.json"));
    const auto update_target = [&id, &target, targetFor] {
      if (!target.isModified()) target.setText(targetFor(id.text().trimmed()));
    };
    connect(&id, &QLineEdit::textEdited, &dialog, update_target);
    connect(&kind, &QComboBox::currentIndexChanged, &dialog, update_target);
    form.addRow(tr("Resource ID"), &id);
    form.addRow(tr("Resource Type"), &kind);
    form.addRow(tr("Target Path"), &target);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("Preview"));
    buttons.button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const QJsonObject input{{"source", source},
                            {"resource_id", id.text().trimmed()},
                            {"kind", kind.currentData().toString()},
                            {"path", target.text().trimmed()}};
    setBusy(true);
    bridge_.request(
        root_, "resource.import.apply", "preview", revision_, input,
        [this, input, source](const QJsonObject& preview) {
          if (preview.value("code") != "ok") {
            reportImportError(preview, source);
            setBusy(false);
            return;
          }
          const auto resource = preview.value("resource_id").toString();
          if (!preview.value("changed").toBool()) {
            report({QT_TR_NOOP("Resource %1 is already imported."), {resource}, {}});
            setBusy(false);
            return;
          }
          const auto digest = preview.value("digest").toString();
          if (digest.isEmpty()) {
            report({nullptr, {}, {{"code", "cli_invalid_result"}, {"location", "preview-import"}}});
            setBusy(false);
            return;
          }
          auto details = tr("Import %1 as %2?\n%3 · %4 bytes")
                             .arg(preview.value("source_name").toString(), resource,
                                  preview.value("path").toString(),
                                  argument(preview.value("bytes").toInteger()));
          if (preview.contains("width"))
            details += "\n" + tr("%1 × %2 px")
                                  .arg(preview.value("width").toInt())
                                  .arg(preview.value("height").toInt());
          else if (preview.contains("line_count"))
            details += "\n" + tr("Lines · %1").arg(preview.value("line_count").toInteger());
          QMessageBox confirm(QMessageBox::Question, tr("Import Preview"), details,
                              QMessageBox::Yes | QMessageBox::Cancel, this);
          confirm.setTextFormat(Qt::PlainText);
          confirm.setDefaultButton(QMessageBox::Cancel);
          confirm.button(QMessageBox::Yes)->setText(tr("Import"));
          confirm.button(QMessageBox::Cancel)->setText(tr("Cancel"));
          if (confirm.exec() != QMessageBox::Yes) {
            cancelPreview("resource.import.apply", input);
            return;
          }
          auto commit = input;
          commit["expected_digest"] = digest;
          bridge_.request(
              root_, "resource.import.apply", "apply", revision_, commit,
              [this, source](const QJsonObject& result) {
                if (result.value("code") != "ok") {
                  reportImportError(result, source);
                  setBusy(false);
                  return;
                }
                report({QT_TR_NOOP("Imported %1."), {result.value("resource_id").toString()}, {}});
                refresh(false);
              });
        });
  }

  void reportImportError(QJsonObject result, const QString& source) {
    if (result.value("location") == "/source") result.insert("resource_path", source);
    report({nullptr, {}, std::move(result)});
  }

  void clearThumbnailCache() {
    if (!clear_thumbnail_cache()) {
      report({QT_TR_NOOP("Could not clear all thumbnails."), {}, {}, "warning"});
      return;
    }
    showResource(resources_->selectedId());
    report({QT_TR_NOOP("Thumbnail cache cleared."), {}, {}});
  }

  void recoverProject() {
    if (QMessageBox::question(
            this, tr("Recover Project…"), tr("Recover the pending transaction in %1?").arg(root_),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
      return;
    }
    setBusy(true);
    bridge_.request(root_, "project.recover", "apply", {}, {}, [this](const QJsonObject& result) {
      if (result.value("code") != "ok") {
        report({nullptr, {}, result});
        setBusy(false);
        return;
      }
      clearProject();
      report({QT_TR_NOOP("Project recovery completed."), {}, {}});
      refresh();
    });
  }

  bool hasDraft() const { return dirty_ || voxel_dirty_; }
  bool playing() const { return session_.has_value() || voxel_preview_.has_value(); }

  bool canDiscard() {
    return !hasDraft() ||
           QMessageBox::question(this, voxel_dirty_ ? tr("Unsaved World") : tr("Unsaved Property"),
                                 voxel_dirty_ ? tr("Discard the unsaved block changes?")
                                              : tr("Discard the unsaved property change?"),
                                 QMessageBox::Discard | QMessageBox::Cancel,
                                 QMessageBox::Cancel) == QMessageBox::Discard;
  }

  void closeProject() {
    if (!canDiscard()) return;
    stop();
    root_.clear();
    clearProject();
    setStatus(QT_TR_NOOP("Project closed."));
  }

  void clearProject() {
    voxel_view_->setWorld(nullptr);
    voxel_preview_.reset();
    voxel_scene_.reset();
    voxel_dirty_ = false;
    views_->setCurrentWidget(viewport_);
    messages_.clear();
    updateOutput();
    viewport_->resetView();
    undo_history_.clear();
    redo_history_.clear();
    project_id_.clear();
    revision_.reset();
    scene_ready_ = false;
    field_schema_ = {};
    entities_ = {};
    resources_model_ = {};
    has_expedition_ = false;
    resources_->setImportRecords({});
    resources_->setPreview({});
    selected_.clear();
    dirty_ = false;
    resources_->setResources(resources_model_);
    setEntities();
    populateInspector();
    updateActions();
  }

  void refresh(bool announce_ready = true) {
    setBusy(true);
    bridge_.request(
        root_, "project.index", "read", {}, {}, [this, announce_ready](const QJsonObject& result) {
          if (result.value("code") != "ok" || !result.value("revision").isDouble() ||
              !result.value("resources").isArray() || !result.value("project_id").isString()) {
            report({nullptr, {}, result});
            setBusy(false);
            emit inspectionFinished(false);
            return;
          }
          project_id_ = result.value("project_id").toString();
          revision_ = result.value("revision").toInteger();
          resources_model_ = result.value("resources").toArray();
          const auto entry = result.value("entry_scene");
          has_expedition_ = std::ranges::any_of(resources_model_, [entry](const auto& row) {
            const auto resource = row.toObject();
            return resource.value("id") == entry &&
                   resource.value("refs").toArray().contains("expedition");
          });
          resources_->setResources(resources_model_);
          const auto imports = result.value("imports").toObject();
          const bool ok = imports.value("code") == "ok";
          resources_->setImportRecords(imports.value("imports").toArray());
          if (!ok) report({nullptr, {}, imports});
          showScene(result.value("scene").toObject(), announce_ready && ok);
        });
  }

  void readScene(bool announce_ready = true) {
    scene_ready_ = false;
    bridge_.request(root_, "scene.inspect", "read", revision_, {},
                    [this, announce_ready](QJsonObject result) {
                      showScene(std::move(result), announce_ready);
                    });
  }

  void showScene(QJsonObject result, bool announce_ready) {
    const bool keep_camera = voxel_scene_.has_value();
    voxel_view_->setWorld(nullptr, false);
    voxel_scene_.reset();
    if (result.value("code") == "ok" && result.value("kind") == "voxel") {
      auto loaded = platform::start_scene(projectPath(), *revision_, true);
      if (loaded.voxel_scene) {
        voxel_scene_ = std::move(loaded.voxel_scene);
        voxel_view_->setWorld(&*voxel_scene_, !keep_camera);
      } else {
        result =
            QJsonDocument::fromJson(QByteArray::fromStdString(platform::scene_result_json(loaded)))
                .object();
      }
    }
    views_->setCurrentWidget(voxel_scene_ ? static_cast<QWidget*>(voxel_view_) : viewport_);
    scene_ready_ =
        result.value("code") == "ok" && result.value("entities").isArray() &&
        result.value("fields").toArray().size() == static_cast<qsizetype>(fields_.size());
    entities_ = scene_ready_ ? result.value("entities").toArray() : QJsonArray{};
    field_schema_ = scene_ready_ ? result.value("fields").toArray() : QJsonArray{};
    populateInspector();
    setEntities();
    const bool ready = scene_ready_ || voxel_scene_.has_value();
    if (ready && announce_ready)
      report({QT_TR_NOOP("Project ready · %1"), {root_}, {}});
    else if (!ready)
      report({nullptr, {}, result});
    setBusy(false);
    if (voxel_scene_) voxel_view_->setFocus(Qt::OtherFocusReason);
    emit inspectionFinished(ready);
  }

  std::filesystem::path projectPath() const {
#ifdef Q_OS_WIN
    return std::filesystem::path(root_.toStdWString());
#else
    return std::filesystem::path(root_.toStdString());
#endif
  }

  void setEntities() {
    scene_tree_->setEntities(entities_);
    scene_tree_->setSelectedId(selected_);
    viewport_->setScene(entities_, false);
    viewport_->setSelectedId(selected_);
  }

  void selectEntity(const QString& id) {
    if (id == selected_) return;
    if (busy_ || playing() || !canDiscard()) {
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

  qsizetype selectedIndex() const {
    for (qsizetype index = 0; index < entities_.size(); ++index)
      if (entities_.at(index).toObject().value("id") == selected_) return index;
    return -1;
  }

  void nameEntity(bool copy) {
    bool accepted = false;
    const auto id = QInputDialog::getText(this, copy ? tr("Duplicate Entity") : tr("Add Entity"),
                                          tr("Entity ID"), QLineEdit::Normal, {}, &accepted)
                        .trimmed();
    if (!accepted || id.isEmpty()) return;
    if (copy)
      changeEntities("copy", selected_, {id}, id);
    else
      changeEntities("add", id, {}, id);
  }

  void removeEntity() {
    const auto response = QMessageBox::question(
        this, tr("Delete Entity"), tr("Delete entity %1 from this scene?").arg(selected_),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (response == QMessageBox::Yes) changeEntities("remove", selected_, {}, {});
  }

  void moveEntity(qsizetype direction) {
    const auto index = selectedIndex();
    if (index >= 0) changeEntities("move", selected_, {argument(index + direction)}, selected_);
  }

  void changeEntities(const QString& operation, const QString& id, const QStringList& extra,
                      const QString& selection) {
    if (!scene_ready_ || !revision_ || busy_ || dirty_ || session_) return;
    QJsonObject input{{"operation", operation}, {"entity_id", id}, {"capture_change", true}};
    if (operation == "move") input["argument"] = extra.value(0).toLongLong();
    if (operation == "copy") input["argument"] = extra.value(0);
    setBusy(true);
    bridge_.request(root_, "scene.entity.apply", "apply", revision_, input,
                    [this, selection](const QJsonObject& result) {
                      if (result.value("code") != "ok" || !result.value("revision").isDouble()) {
                        report({nullptr, {}, result});
                        setBusy(false);
                        return;
                      }
                      recordHistory(result, selection);
                      revision_ = result.value("revision").toInteger();
                      selected_ = selection;
                      readScene();
                    });
  }

  void recordHistory(const QJsonObject& result, const QString& next_selection) {
    if (!result.value("changed").toBool()) return;
    redo_history_.clear();
    auto change = result.value("scene_change").toObject();
    if (change.isEmpty()) change = result.value("change").toObject();
    if (change.isEmpty()) {
      undo_history_.clear();
      return;
    }
    if (undo_history_.size() == 32) undo_history_.pop_front();
    undo_history_.push_back({change, selected_, next_selection});
  }

  void restoreHistory(bool redo) {
    auto& source = redo ? redo_history_ : undo_history_;
    if (!(scene_ready_ || voxel_scene_) || !revision_ || busy_ || hasDraft() || playing() ||
        source.empty())
      return;
    auto request = source.back().change;
    const bool project = request.contains("resources");
    if (!redo) {
      const auto reverse = [](QJsonObject resource) {
        const auto before = resource.value("before");
        resource["before"] = resource.value("after");
        resource["after"] = before;
        return resource;
      };
      if (project) {
        QJsonArray reversed;
        for (const auto row : request.value("resources").toArray())
          reversed.append(reverse(row.toObject()));
        request["resources"] = reversed;
      } else {
        request = reverse(request);
      }
    }
    setBusy(true);
    bridge_.request(
        root_, project ? "project.restore" : "scene.restore", "apply", revision_,
        {{"change", request}}, [this, redo, project](const QJsonObject& result) {
          if (result.value("code") != "ok" || !result.value("changed").toBool() ||
              !result.value("revision").isDouble()) {
            report({nullptr, {}, result});
            setBusy(false);
            return;
          }
          auto& from = redo ? redo_history_ : undo_history_;
          auto& to = redo ? undo_history_ : redo_history_;
          selected_ = redo ? from.back().after_selection : from.back().before_selection;
          to.push_back(std::move(from.back()));
          from.pop_back();
          revision_ = result.value("revision").toInteger();
          report({redo ? QT_TR_NOOP("Change redone.") : QT_TR_NOOP("Change undone."), {}, {}});
          if (project)
            refresh(false);
          else
            readScene(false);
        });
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
    resources_->setPreview({});
    if (id.isEmpty()) return;
    for (const auto item : resources_model_) {
      const auto resource = item.toObject();
      if (resource.value("id") != id) continue;
      const auto path = resource.value("path").toString();
      setStatus(nullptr, {path});
      const auto kind = resource.value("kind").toString();
      if (kind != "image" && kind != "text") return;
#ifdef Q_OS_WIN
      const std::filesystem::path directory(root_.toStdWString());
#else
      const std::filesystem::path directory(root_.toStdString());
#endif
      const auto source = platform::read_asset_source(directory, id.toStdString());
      if (source.code != "ok") {
        report({nullptr,
                {},
                {{"code", QString::fromStdString(source.code)},
                 {"location", QString::fromStdString(source.location)}}});
        return;
      }
      auto preview = preview_asset(kind.toStdString(), source.bytes);
      if (preview.code != "ok") {
        report({nullptr,
                {},
                {{"code", QString::fromLatin1(preview.code.data(),
                                              static_cast<qsizetype>(preview.code.size()))},
                 {"location", path}}});
        return;
      }
      resources_->setPreview(std::move(preview));
      return;
    }
  }

  void discardProperty() {
    if (voxel_scene_) {
      if (!voxel_dirty_ || busy_) return;
      voxel_view_->setWorld(nullptr);
      voxel_scene_.reset();
      voxel_dirty_ = false;
      setBusy(true);
      readScene();
      return;
    }
    dirty_ = false;
    populateInspector();
    updateActions();
  }

  void saveProperty() {
    if (voxel_scene_) {
      if (!voxel_dirty_ || busy_) return;
      setBusy(true);
      const auto prepared = platform::prepare_voxel_save(*voxel_scene_);
      const auto reply =
          QJsonDocument::fromJson(QByteArray::fromStdString(platform::field_result_json(prepared)))
              .object();
      if (!prepared.scene_change) {
        report({nullptr, {}, reply});
        setBusy(false);
        return;
      }
      bridge_.request(
          root_, "scene.restore", "apply", revision_, {{"change", reply.value("scene_change")}},
          [this](const QJsonObject& result) {
            if (result.value("code") != "ok" || !result.value("revision").isDouble()) {
              report({nullptr, {}, result});
              setBusy(false);
              return;
            }
            voxel_dirty_ = false;
            recordHistory(result, {});
            revision_ = result.value("revision").toInteger();
            report({QT_TR_NOOP("World saved · revision %1"), {argument(*revision_)}, {}});
            readScene(false);
          });
      return;
    }
    if (!dirty_ || busy_ || !revision_) return;
    const auto field = field_schema_.at(static_cast<qsizetype>(editing_)).toObject().value("id");
    const auto value = fields_[editing_]->value();
    const QJsonObject input{{"entity_id", selected_},
                            {"field", field},
                            {"value", value},
                            {"expected_value", committed_[editing_]},
                            {"capture_change", true}};
    setBusy(true);
    bridge_.request(root_, "scene.field.set", "preview", revision_, input,
                    [this, input](const QJsonObject& preview) {
                      if (preview.value("code") != "ok") {
                        report({nullptr, {}, preview});
                        setBusy(false);
                        return;
                      }
                      bridge_.request(root_, "scene.field.set", "apply", revision_, input,
                                      [this](const QJsonObject& result) {
                                        if (result.value("code") != "ok" ||
                                            !result.value("revision").isDouble()) {
                                          report({nullptr, {}, result});
                                          setBusy(false);
                                          return;
                                        }
                                        recordHistory(result, selected_);
                                        revision_ = result.value("revision").toInteger();
                                        dirty_ = false;
                                        report({QT_TR_NOOP("Saved %1 · %2 → %3"),
                                                {result.value("location").toString(),
                                                 argument(result.value("old_value").toInteger()),
                                                 argument(result.value("new_value").toInteger())},
                                                {}});
                                        readScene(false);
                                      });
                    });
  }

  void editDataField() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Integer Data Field…"));
    QFormLayout form(&dialog);
    QLineEdit resource(resources_->selectedId(), &dialog);
    QLineEdit field(&dialog);
    QLineEdit value(&dialog);
    form.addRow(tr("Resource ID"), &resource);
    form.addRow(tr("Field"), &field);
    form.addRow(tr("New integer value"), &value);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.button(QDialogButtonBox::Ok)->setText(tr("Preview"));
    buttons.button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    bool is_integer = false;
    const auto new_integer = value.text().trimmed().toLongLong(&is_integer);
    if (!is_integer) {
      report({nullptr, {}, {{"code", "invalid_argument"}, {"location", "/input/value"}}});
      return;
    }
    const QJsonObject input{
        {"resource_id", resource.text()}, {"field", field.text()}, {"value", new_integer}};
    setBusy(true);
    bridge_.request(root_, "resource.field.set", "preview", revision_, input,
                    [this, input](const QJsonObject& preview) {
                      if (preview.value("code") != "ok") {
                        report({nullptr, {}, preview});
                        setBusy(false);
                        return;
                      }
                      const auto old_value = argument(preview.value("old_value").toInteger());
                      const auto new_value = argument(preview.value("new_value").toInteger());
                      if (!preview.value("changed").toBool()) {
                        report({QT_TR_NOOP("The data field already has this value."), {}, {}});
                        setBusy(false);
                        return;
                      }
                      if (QMessageBox::question(
                              this, tr("Preview"),
                              tr("Apply %1 · %2 → %3?")
                                  .arg(preview.value("location").toString(), old_value, new_value),
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes) {
                        cancelPreview("resource.field.set", input);
                        return;
                      }
                      bridge_.request(root_, "resource.field.set", "apply", revision_, input,
                                      [this](const QJsonObject& result) {
                                        if (result.value("code") != "ok" ||
                                            !result.value("revision").isDouble()) {
                                          report({nullptr, {}, result});
                                          setBusy(false);
                                          return;
                                        }
                                        revision_ = result.value("revision").toInteger();
                                        report({QT_TR_NOOP("Saved %1 · %2 → %3"),
                                                {result.value("location").toString(),
                                                 argument(result.value("old_value").toInteger()),
                                                 argument(result.value("new_value").toInteger())},
                                                {}});
                                        readScene(false);
                                      });
                    });
  }

  void cancelPreview(const QString& operation, const QJsonObject& input) {
    bridge_.request(root_, operation, "cancel", revision_, input,
                    [this](const QJsonObject& result) {
                      if (result.value("code") == "cancelled")
                        setStatus(QT_TR_NOOP("Preview cancelled."));
                      else
                        report({nullptr, {}, result});
                      setBusy(false);
                    });
  }

  void changePalette() {
    if (!voxel_scene_ || !revision_ || busy_ || playing() || hasDraft()) return;
    const QStringList labels{tr("Daylight"), tr("Amber Dusk")};
    const QStringList ids{"daylight", "amber-dusk"};
    bool accepted = false;
    const auto selected = QInputDialog::getItem(
        this, tr("Voxel Palette"), tr("Palette Plugin"), labels,
        static_cast<int>(ids.indexOf(QString::fromStdString(voxel_scene_->palette))), false,
        &accepted);
    if (!accepted) return;
    const auto provider = ids.at(labels.indexOf(selected));
    setBusy(true);
    bridge_.request(root_, "scene.palette.set", "apply", revision_, {{"provider", provider}},
                    [this](const QJsonObject& result) {
                      if (result.value("code") != "ok") {
                        report({nullptr, {}, result});
                        setBusy(false);
                        return;
                      }
                      recordHistory(result, {});
                      revision_ = result.value("revision").toInteger();
                      readScene();
                    });
  }

  void openWorkshop() {
    if (busy_) return;
    auto* viewport_scene = voxel_preview_ ? &*voxel_preview_
                           : voxel_scene_ ? &*voxel_scene_
                                          : nullptr;
    if (viewport_scene && viewport_scene->workshop) {
      auto& workshop = *viewport_scene->workshop;
      if (show_workshop(this, workshop, false,
                        [&](const items::Action& action) { return workshop.apply(action); }) &&
          !voxel_preview_)
        voxel_dirty_ = true;
      voxel_view_->setFocus(Qt::OtherFocusReason);
      updateActions();
    } else if (session_ && session_->workshop()) {
      show_workshop(this, *session_->workshop(), true,
                    [&](const items::Action& action) { return session_->work(action); });
      viewport_->setFocus(Qt::OtherFocusReason);
    }
  }

  void openRecipeGraph() {
    if (!recipes_->isEnabled()) return;
    setBusy(true);
    bridge_.request(root_, "expedition.recipes", "read", revision_, {},
                    [this](const QJsonObject& result) {
                      if (result.value("code") == "ok")
                        show_recipe_graph(this, result);
                      else
                        report({nullptr, {}, result});
                      setBusy(false);
                    });
  }

  void play() {
    if (busy_ || playing() || hasDraft() || !revision_) return;
    if (voxel_scene_) {
      voxel_preview_ = *voxel_scene_;
      voxel_view_->setWorld(&*voxel_preview_);
      voxel_view_->setFocus(Qt::OtherFocusReason);
      setStatus(QT_TR_NOOP("Playing a copy of the world. Select Stop to edit."));
      updateActions();
      return;
    }
    if (!scene_ready_) return;
    auto result = platform::start_scene(projectPath(), static_cast<std::uint64_t>(*revision_));
    if (!result.session) {
      report({nullptr,
              {},
              {{"code", QString::fromStdString(result.code)},
               {"resource_path", QString::fromStdString(result.resource_path)},
               {"location", QString::fromStdString(result.location)}}});
      return;
    }
    session_ = std::move(result.session);
    viewport_->beginPlayback(*session_);
    if (session_->snapshot().interactive) {
      play_focus_ = focusWidget();
      viewport_->setFocus(Qt::OtherFocusReason);
    }
    drawSession();
    clock_ = {};
    play_elapsed_.start();
    last_play_ns_ = 0;
    timer_.start();
    updateActions();
  }

  void advancePlayback() {
    const auto now = play_elapsed_.nsecsElapsed();
    const auto elapsed = std::chrono::nanoseconds(now - last_play_ns_);
    last_play_ns_ = now;
    const auto& snapshot = session_->snapshot();
    if (snapshot.interactive && (!viewport_->hasFocus() || !isActiveWindow())) {
      setStatus(QT_TR_NOOP("Paused. Click the viewport to resume."));
      return;
    }
    const auto count =
        std::min(clock_.consume(elapsed), platform::SceneSession::max_ticks - snapshot.ticks);
    if (count == 0) return;
    session_->advance(count, viewport_->sceneInput());
    drawSession();
    if (snapshot.outcome != platform::SceneOutcome::playing ||
        snapshot.ticks == platform::SceneSession::max_ticks)
      timer_.stop();
  }

  void drawSession() {
    QJsonArray entities;
    for (const auto& entity : session_->snapshot().entities) {
      entities.append(QJsonObject{{"id", QString::fromStdString(entity.id)},
                                  {"x", static_cast<qint64>(entity.x)},
                                  {"y", static_cast<qint64>(entity.y)},
                                  {"half_width", static_cast<qint64>(entity.half_width)},
                                  {"half_height", static_cast<qint64>(entity.half_height)},
                                  {"sprite", QString::fromStdString(entity.sprite)},
                                  {"role", static_cast<int>(entity.role)}});
    }
    viewport_->setScene(std::move(entities), true);
    const auto& snapshot = session_->snapshot();
    switch (snapshot.outcome) {
      case platform::SceneOutcome::won:
        setStatus(QT_TR_NOOP("Delivered! Select Stop to play again."));
        return;
      case platform::SceneOutcome::lost:
        setStatus(QT_TR_NOOP("Delivery failed. Select Stop to play again."));
        return;
      case platform::SceneOutcome::timed_out:
        setStatus(QT_TR_NOOP("Time is up. Select Stop to play again."));
        return;
      default:
        break;
    }
    if (snapshot.ticks == platform::SceneSession::max_ticks) {
      setStatus(QT_TR_NOOP("Playback reached its final tick. Select Stop to edit."));
    } else if (snapshot.interactive) {
      const auto rate = platform::SceneClock::ticks_per_second;
      const auto remaining = (platform::SceneSession::max_ticks - snapshot.ticks + rate - 1) / rate;
      setStatus(QT_TR_NOOP("Playing · %1 s remaining"), {QString::number(remaining)});
    } else {
      setStatus(QT_TR_NOOP("Playing · tick %1"), {QString::number(snapshot.ticks)});
    }
  }

  void stop() {
    if (voxel_preview_) {
      voxel_view_->setWorld(voxel_scene_ ? &*voxel_scene_ : nullptr);
      voxel_preview_.reset();
      updateActions();
      setStatus(QT_TR_NOOP("Playback stopped."));
    }
    if (!session_) return;
    timer_.stop();
    session_.reset();
    viewport_->setScene(entities_, false);
    updateActions();
    if (play_focus_) play_focus_->setFocus(Qt::OtherFocusReason);
    play_focus_.clear();
    setStatus(QT_TR_NOOP("Playback stopped."));
  }

  void setStatus(const char* pattern, QStringList arguments = {}) {
    status_ = {pattern, std::move(arguments), {}};
    statusBar()->showMessage(status_.text());
  }

  void updateOutput() {
    QJsonArray entries;
    for (const auto& message : messages_)
      entries.append(QJsonObject{{"text", message.text()}, {"level", message.severity()}});
    output_->setEntries(entries);
  }

  void report(Message message) {
    if (messages_.size() == 200) messages_.pop_front();
    status_ = message;
    messages_.push_back(std::move(message));
    updateOutput();
    statusBar()->showMessage(status_.text());
  }

  void setBusy(bool busy) {
    busy_ = busy;
    updateActions();
  }

  void updateActions() {
    const bool editable = scene_ready_ && !selected_.isEmpty() && !busy_ && !session_;
    for (std::size_t index = 0; index < fields_.size(); ++index)
      fields_[index]->setEnabled(editable && (!dirty_ || index == editing_));
    scene_tree_->setEnabled(!busy_ && !session_ && !voxel_scene_);
    voxel_view_->setEnabled(!busy_);
    resources_->setEnabled(!busy_);
    open_->setEnabled(!busy_);
    reload_->setEnabled(!busy_ && !root_.isEmpty());
    create_->setEnabled(!busy_);
    close_->setEnabled(!busy_ && !root_.isEmpty());
    import_->setEnabled(!busy_ && !playing() && !hasDraft() && revision_.has_value());
    merge_->setEnabled(import_->isEnabled() && !resources_model_.isEmpty());
    export_->setEnabled(!busy_ && !playing() && (scene_ready_ || voxel_scene_) && !hasDraft() &&
                        revision_.has_value());
    clear_cache_->setEnabled(!busy_ && revision_.has_value());
    recover_->setEnabled(!busy_ && !playing() && !hasDraft() && !root_.isEmpty());
    operations_->setEnabled(!busy_);
    edit_data_->setEnabled(!busy_ && !playing() && !hasDraft() && revision_.has_value());
    save_->setText(voxel_scene_ ? tr("&Save World") : tr("&Save Property"));
    revert_->setText(voxel_scene_ ? tr("Discard World Draft") : tr("Discard Property Draft"));
    save_->setEnabled((editable && dirty_) || (voxel_scene_ && voxel_dirty_ && !busy_));
    revert_->setEnabled(save_->isEnabled());
    const bool history_editable =
        (scene_ready_ || voxel_scene_) && !busy_ && !playing() && !hasDraft() && revision_;
    undo_->setEnabled(history_editable && !undo_history_.empty());
    redo_->setEnabled(history_editable && !redo_history_.empty());
    const bool structure_editable = scene_ready_ && history_editable;
    const auto index = selectedIndex();
    add_entity_->setEnabled(structure_editable);
    copy_entity_->setEnabled(structure_editable && index >= 0);
    remove_entity_->setEnabled(structure_editable && index >= 0);
    move_up_->setEnabled(structure_editable && index > 0);
    move_down_->setEnabled(structure_editable && index >= 0 && index + 1 < entities_.size());
    play_->setEnabled((scene_ready_ || voxel_scene_) && !busy_ && !playing() && !hasDraft() &&
                      revision_.has_value());
    stop_->setEnabled(playing());
    palette_->setEnabled(voxel_scene_ && !busy_ && !playing() && !hasDraft() && revision_);
    workshop_->setEnabled(
        !busy_ && ((voxel_scene_ && voxel_scene_->workshop) || (session_ && session_->workshop())));
    recipes_->setEnabled(!busy_ && !playing() && revision_ && has_expedition_);
    const auto name = voxel_scene_ && !voxel_scene_->title.empty()
                          ? QString::fromStdString(voxel_scene_->title)
                          : project_id_;
    project_label_->setText(name.isEmpty() ? tr("No project open") : name);
    const auto revision = revision_ ? tr("Revision %1").arg(*revision_) : QString{};
    revision_label_->setText(revision + (hasDraft() ? tr(" · Unsaved") : QString{}));
    draft_label_->setText(
        dirty_ ? tr("Unsaved · %1 → %2").arg(committed_[editing_]).arg(fields_[editing_]->value())
               : QString{});
    draft_label_->setVisible(dirty_);
    setWindowTitle(name.isEmpty() ? tr("WDS Studio")
                                  : tr("%1%2 — WDS Studio").arg(name, hasDraft() ? "*" : ""));
  }

  LanguageSettings& language_;
  ProjectBridge bridge_{this};
  Message status_;
  std::deque<Message> messages_;
  std::deque<HistoryEntry> undo_history_;
  std::deque<HistoryEntry> redo_history_;
  QTimer timer_;
  QElapsedTimer play_elapsed_;
  qint64 last_play_ns_ = 0;
  platform::SceneClock clock_;
  QPointer<QWidget> play_focus_;
  Viewport* viewport_;
  VoxelViewport* voxel_view_;
  QStackedWidget* views_;
  SceneTreePanel* scene_tree_;
  ResourcePanel* resources_;
  OutputPanel* output_;
  QLabel* project_label_;
  QLabel* revision_label_;
  QLabel* entity_label_;
  QLabel* draft_label_;
  QMenu* project_menu_;
  QMenu* edit_menu_;
  QMenu* scene_menu_;
  QMenu* view_menu_;
  QMenu* settings_menu_;
  QToolBar* toolbar_;
  QDockWidget* hierarchy_;
  QDockWidget* inspector_;
  QDockWidget* assets_;
  QDockWidget* output_dock_;
  QAction* settings_action_;
  QAction* open_;
  QAction* reload_;
  QAction* create_;
  QAction* close_;
  QAction* import_;
  QAction* merge_;
  QAction* export_;
  QAction* clear_cache_;
  QAction* recover_;
  QAction* operations_;
  QAction* edit_data_;
  QAction* save_;
  QAction* undo_;
  QAction* redo_;
  QAction* revert_;
  QAction* play_;
  QAction* stop_;
  QAction* workshop_;
  QAction* recipes_;
  QAction* palette_;
  QAction* add_entity_;
  QAction* copy_entity_;
  QAction* remove_entity_;
  QAction* move_up_;
  QAction* move_down_;
  std::array<QSpinBox*, platform::scene_fields.size()> fields_{};
  std::array<QLabel*, platform::scene_fields.size()> field_labels_{};
  std::array<int, platform::scene_fields.size()> committed_{};
  QString root_;
  QString project_id_;
  QString selected_;
  std::optional<qint64> revision_;
  std::optional<platform::SceneSession> session_;
  std::optional<platform::VoxelScene> voxel_scene_;
  std::optional<platform::VoxelScene> voxel_preview_;
  QJsonArray entities_;
  QJsonArray resources_model_;
  QJsonArray field_schema_;
  bool busy_ = false;
  bool dirty_ = false;
  bool voxel_dirty_ = false;
  bool scene_ready_ = false;
  bool has_expedition_ = false;
  std::size_t editing_ = 0;
};

void run_recipe_smoke(QApplication& app, Workbench& window, const QString& project) {
  QObject::connect(&window, &Workbench::inspectionFinished, &app, [&](bool ready) {
    auto* action = window.findChild<QAction*>("expeditionRecipes");
    if (!ready || !action || !action->isEnabled()) {
      app.exit(1);
      return;
    }
    auto* watch = new QTimer(&app);
    QObject::connect(watch, &QTimer::timeout, &app, [&, watch] {
      auto* dialog = window.findChild<QDialog*>("recipeGraph");
      if (!dialog || !dialog->isVisible()) return;
      watch->stop();
      auto* selection = dialog->findChild<QComboBox*>("dependencyRecipe");
      auto* tree = dialog->findChild<QTreeWidget*>("dependencyTree");
      bool checked = false;
      if (selection && tree && selection->findText("garden-stew") >= 0) {
        selection->setCurrentText("garden-stew");
        constexpr auto matching = Qt::MatchContains | Qt::MatchRecursive;
        const auto links = tree->findItems("build-stove", matching);
        checked = !tree->findItems("#aromatic", matching).empty() &&
                  !tree->findItems("wind-garden / plot", matching).empty() && links.size() == 1;
        if (checked) {
          tree->itemActivated(links.front(), 0);
          checked = selection->currentText() == "build-stove" &&
                    !tree->findItems("hearth", matching).empty();
        }
      }
      dialog->reject();
      app.exit(checked ? 0 : 1);
    });
    watch->start(10);
    action->trigger();
  });
  QTimer::singleShot(20000, &app, [&] { app.exit(2); });
  window.openProject(project);
}

bool weather_preview_ok(QWidget* viewport) {
  if (!viewport || !viewport->isVisible()) return false;
  const auto frame = [viewport] {
    const auto image = viewport->grab().toImage();
    return image.copy(image.width() / 4, image.height() / 5, image.width() / 2, image.height() / 2);
  };
  const auto press = [viewport](Qt::Key key) {
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
  };
  const auto clear = frame();
  press(Qt::Key_P);
  const auto rain = frame();
  press(Qt::Key_L);
  const auto night = frame();
  press(Qt::Key_L);
  return !clear.isNull() && !rain.isNull() && !night.isNull() && clear != rain && rain != night &&
         frame() == rain;
}

}  // namespace
}  // namespace wds::studio

#include "main.moc"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  const auto arguments = app.arguments();
  const bool smoke =
      (arguments.size() == 3 || arguments.size() == 4) && arguments.at(1) == "--smoke";
  const bool edit_smoke = arguments.size() == 3 && arguments.at(1) == "--smoke-edit";
  const bool recipe_smoke = arguments.size() == 3 && arguments.at(1) == "--smoke-recipes";
  const bool weather_smoke = arguments.size() == 3 && arguments.at(1) == "--smoke-weather";
  QCoreApplication::setOrganizationName("WDS");
  QCoreApplication::setApplicationName(
      smoke || edit_smoke || recipe_smoke || weather_smoke ? "WDS Studio Smoke" : "WDS Studio");
  wds::studio::theme::apply(app);
  wds::studio::LanguageSettings language;
  wds::studio::Workbench window(language);
  window.show();
  if (smoke) {
    QObject::connect(&window, &wds::studio::Workbench::inspectionFinished, &app, [&](bool ready) {
      const auto frame = ready ? window.centralWidget()->grab() : QPixmap{};
      app.exit(!frame.isNull() && (arguments.size() == 3 || frame.save(arguments.at(3))) ? 0 : 1);
    });
    QTimer::singleShot(20000, &app, [&] { app.exit(2); });
    window.openProject(arguments.at(2));
  } else if (edit_smoke) {
    bool editing = false;
    QObject::connect(&window, &wds::studio::Workbench::inspectionFinished, &app, [&](bool ready) {
      if (!ready || editing) {
        app.exit(ready && window.editSmokeSaved() ? 0 : 1);
        return;
      }
      editing = true;
      if (!window.startEditSmoke()) app.exit(1);
    });
    QTimer::singleShot(20000, &app, [&] { app.exit(2); });
    window.openProject(arguments.at(2));
  } else if (recipe_smoke) {
    wds::studio::run_recipe_smoke(app, window, arguments.at(2));
  } else if (weather_smoke) {
    QObject::connect(&window, &wds::studio::Workbench::inspectionFinished, &app, [&](bool ready) {
      auto* viewport = window.findChild<QWidget*>("voxelViewport");
      app.exit(ready && wds::studio::weather_preview_ok(viewport) ? 0 : 1);
    });
    QTimer::singleShot(20000, &app, [&] { app.exit(2); });
    window.openProject(arguments.at(2));
  } else if (arguments.size() == 2) {
    window.openProject(arguments.at(1));
  }
  return app.exec();
}
