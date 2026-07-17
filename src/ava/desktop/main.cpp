#include "sys.h"
#include "desktop_controller.h"
#include "ava/core/version.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <string_view>

namespace {

[[nodiscard]] QString to_qstring(std::string_view value)
{
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

}  // namespace

int main(int argc, char** argv)
{
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("AVA Desktop"));
  QCoreApplication::setApplicationVersion(to_qstring(ava::core::version::kFullVersion));
  QGuiApplication::setOrganizationName(QStringLiteral("Artificial Source"));

  ava::desktop::DesktopController controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("avaDesktop"), &controller);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Ava.Desktop"), QStringLiteral("Main"));

  return QGuiApplication::exec();
}
