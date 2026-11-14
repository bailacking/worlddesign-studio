#include "language.hpp"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QSettings>

namespace wds::studio {
namespace {

constexpr auto language_key = "workbench/language";

}  // namespace

LanguageSettings::LanguageSettings() {
  const bool available = workbench_.load(":/studio/i18n/workbench_zh_CN.qm");
  if (available && QSettings().value(language_key, "zh_CN").toString() != "en") {
    QCoreApplication::installTranslator(&workbench_);
    language_ = "zh_CN";
  }
  // A deployed app uses the adjacent translations directory. A development build can use the SDK.
  if (!qt_.load("qt_zh_CN", QCoreApplication::applicationDirPath() + "/translations"))
    (void)qt_.load("qtbase_zh_CN", QLibraryInfo::path(QLibraryInfo::TranslationsPath));
  if (language_ == "zh_CN" && !qt_.isEmpty()) QCoreApplication::installTranslator(&qt_);
}

const QString& LanguageSettings::language() const { return language_; }

bool LanguageSettings::setLanguage(const QString& language) {
  if (language == language_) return true;
  if (language != "en" && (language != "zh_CN" || workbench_.isEmpty())) return false;

  // Persist first so a settings write failure leaves the running language unchanged.
  QSettings settings;
  settings.setValue(language_key, language);
  settings.sync();
  if (settings.status() != QSettings::NoError) return false;

  language_ = language;
  if (language_ == "zh_CN") {
    if (!qt_.isEmpty()) QCoreApplication::installTranslator(&qt_);
    QCoreApplication::installTranslator(&workbench_);
  } else {
    if (!qt_.isEmpty()) QCoreApplication::removeTranslator(&qt_);
    QCoreApplication::removeTranslator(&workbench_);
  }
  return true;
}

}  // namespace wds::studio
