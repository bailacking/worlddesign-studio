#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>
#include <optional>

namespace wds::studio {

class ProjectBridge final : public QObject {
 public:
  using Completion = std::function<void(QJsonObject)>;
  explicit ProjectBridge(QObject* parent);
  void request(const QString& directory, const QString& operation, const QString& mode,
               std::optional<qint64> revision, const QJsonObject& input, Completion complete);
  void discover(Completion complete);
  qint64 requests() const { return requests_; }
  double elapsedMs() const { return elapsed_ms_; }

 private:
  void run(QStringList arguments, Completion complete, QByteArray input, bool may_write);
  qint64 requests_ = 0;
  double elapsed_ms_ = 0;
};

}  // namespace wds::studio
