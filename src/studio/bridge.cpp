#include "bridge.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace wds::studio {

ProjectBridge::ProjectBridge(QObject* parent) : QObject(parent) {}

void ProjectBridge::request(const QString& directory, const QString& operation, const QString& mode,
                            std::optional<qint64> revision, const QJsonObject& input,
                            Completion complete) {
  QJsonObject body{{"operation", operation}, {"version", 1}, {"mode", mode}, {"input", input}};
  if (revision) body.insert("expected_revision", *revision);
  QStringList arguments{"project", "request", directory};
  const bool apply = mode == "apply";
  if (apply)
    arguments.append(operation == "project.export" ? "--allow-destination-write"
                                                   : "--allow-project-write");
  run(
      std::move(arguments),
      [this, complete = std::move(complete)](QJsonObject result) {
        const auto usage = result.value("usage").toObject();
        requests_ += usage.value("requests").toInteger();
        elapsed_ms_ += usage.value("elapsed_ms").toDouble();
        complete(std::move(result));
      },
      QJsonDocument(body).toJson(QJsonDocument::Compact), apply);
}

void ProjectBridge::discover(Completion complete) {
  run({"--project-operations"}, std::move(complete), {}, false);
}

void ProjectBridge::run(QStringList arguments, Completion complete, QByteArray input,
                        bool may_write) {
  auto* process = new QProcess(this);
  auto* deadline = new QTimer(process);
  deadline->setSingleShot(true);
  auto timed_out = std::make_shared<bool>(false);
  const auto finish = [process, deadline, complete = std::move(complete)](QJsonObject result) {
    deadline->stop();
    process->deleteLater();
    complete(std::move(result));
  };
  connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError e) {
    if (e == QProcess::FailedToStart)
      finish({{"code", "cli_unavailable"}, {"location", process->program()}});
  });
  connect(process, &QProcess::finished, process,
          [process, finish, timed_out](int code, QProcess::ExitStatus status) {
            const auto doc = QJsonDocument::fromJson(process->readAllStandardOutput());
            const auto result = doc.object();
            const auto outcome = result.value("code");
            if (!*timed_out && status == QProcess::NormalExit && outcome.isString() &&
                ((code == 0 && outcome == "ok") || (code == 1 && outcome != "ok"))) {
              finish(result);
            } else {
              finish({{"code", *timed_out ? "cli_timeout" : "cli_invalid_result"},
                      {"location", process->arguments().value(1)}});
            }
          });
  if (!may_write) {
    connect(deadline, &QTimer::timeout, process, [process, timed_out] {
      *timed_out = true;
      process->kill();
    });
    deadline->start(15000);
  }
  connect(process, &QProcess::started, process, [process, input = std::move(input)] {
    if (!input.isEmpty()) process->write(input);
    process->closeWriteChannel();
  });
  auto executable = QCoreApplication::applicationDirPath() + "/wds_cli";
#ifdef Q_OS_WIN
  executable += ".exe";
  process->setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
  process->start(executable, std::move(arguments));
}

}  // namespace wds::studio
