#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

namespace wds::studio {

class ProjectBridge final : public QObject {
 public:
  explicit ProjectBridge(QObject* parent);
  void run(QStringList arguments, std::function<void(QJsonObject)> complete);
};

}  // namespace wds::studio
