#pragma once

#include <QString>
#include <QTranslator>

namespace wds::studio {

class LanguageSettings {
 public:
  LanguageSettings();
  [[nodiscard]] const QString& language() const;
  [[nodiscard]] bool setLanguage(const QString& language);

 private:
  QTranslator workbench_;
  QTranslator qt_;
  QString language_ = QStringLiteral("en");
};

}  // namespace wds::studio
