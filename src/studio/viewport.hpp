#pragma once

#include <QJsonArray>
#include <QWidget>
#include <functional>

namespace wds::studio {

class Viewport final : public QWidget {
 public:
  explicit Viewport(std::function<void(QString)> select, QWidget* parent = nullptr);
  void setScene(QJsonArray entities, bool running);
  void setSelectedId(QString id);

 protected:
  void changeEvent(QEvent* event) override;
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  QPointF screenPosition(const QJsonObject& entity) const;

  QJsonArray entities_;
  bool running_ = false;
  QString selected_;
  std::function<void(QString)> select_;
};

}  // namespace wds::studio
