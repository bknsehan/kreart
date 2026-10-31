#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QIcon>
#include <filesystem>

#include "kreart/ui/app_settings.h"
#include "kreart/ui/main_window.h"
#include "kreart/ui/android_window.h"
#include "kreart/ui/desktop_theme.h"

int main(int argc, char *argv[]) {
  Q_INIT_RESOURCE(kreart);
  QApplication app(argc, argv);
  app.setApplicationDisplayName("Kreart");
  app.setOrganizationDomain("io.github.kreart");
  app.setOrganizationName("Kreart");
  app.setWindowIcon(QIcon(":/icons/icons/io.github.kreart.Kreart.svg"));
  app.setApplicationVersion(KREART_VERSION_STRING);

  // Initialize application settings
  auto &settings = kreart::ui::AppSettings::instance();
  settings.load();
  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate("main", "Fast, native digital painting application with a Krita-compatible core"));
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption mobileOption({"m", "mobile"}, QCoreApplication::translate("main", "Launch in Android/mobile UI mode (Gallery + mobile workspace)."));
  parser.addOption(mobileOption);

  parser.addPositionalArgument("file", QCoreApplication::translate("main", "Image or Krita document to open."));
  parser.process(app);

  const QStringList positional_args = parser.positionalArguments();

#if defined(Q_OS_ANDROID)
  const bool use_mobile = true;
#else
  const bool use_mobile = parser.isSet(mobileOption) || qEnvironmentVariableIsSet("KREART_MOBILE");
#endif

  if (use_mobile) {
    auto *mobile_window = new kreart::ui::AndroidAppWindow();
#if defined(Q_OS_ANDROID)
    // Keep the system status/navigation bars: showMaximized() lets the OS inset
    // the content instead of drawing under the bars.
    mobile_window->showMaximized();
#else
    mobile_window->show();
#endif
    if (!positional_args.isEmpty()) {
      mobile_window->open_file(positional_args.first().toStdString());
    }
    return app.exec();
  } else {
    kreart::ui::apply_desktop_theme();
    auto *desktop_window = new kreart::ui::MainWindow();
    if (!positional_args.isEmpty()) {
      desktop_window->open_file(positional_args.first().toStdString());
      desktop_window->show();
    } else {
      if (kreart::ui::AppSettings::instance().startup_mode() == kreart::ui::StartupMode::GALLERY) {
        desktop_window->show_initial_gallery();
      } else {
        desktop_window->show();
      }
    }
    return app.exec();
  }
}
