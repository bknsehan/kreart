#include "kreart/mobile/android_utils.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>

#include "kreart/ui/app_settings.h"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#include <QFile>
#include <QSet>
#include <QtCore/private/qandroidextras_p.h>
#endif

namespace kreart::mobile {
namespace {

const QString kReserved = QStringLiteral("/\\:*?\"<>|");

bool contains_reserved(const QString &stem) {
  for (QChar ch : stem) {
    if (kReserved.contains(ch)) return true;
    if (ch.unicode() < 0x20) return true;
  }
  return false;
}

} // namespace

bool is_valid_file_stem(const QString &stem, QString &error) {
  const QString trimmed = stem.trimmed();
  if (trimmed.isEmpty()) {
    error = QObject::tr("Name cannot be empty");
    return false;
  }
  if (trimmed == "." || trimmed == "..") {
    error = QObject::tr("Invalid name");
    return false;
  }
  if (contains_reserved(trimmed)) {
    error = QObject::tr("Name cannot contain / \\ : * ? \" < > |");
    return false;
  }
  return true;
}

std::filesystem::path unique_path_in(const std::filesystem::path &dir,
                                     const std::string &stem,
                                     const std::string &extension) {
  std::error_code ec;
  std::filesystem::path candidate = dir / (stem + extension);
  if (!std::filesystem::exists(candidate, ec)) return candidate;
  for (int i = 2; i < 10000; ++i) {
    candidate = dir / (stem + " " + std::to_string(i) + extension);
    if (!std::filesystem::exists(candidate, ec)) return candidate;
  }
  return dir / (stem + " " + std::to_string(10000) + extension);
}

std::filesystem::path mobile_projects_dir() {
  // 1. A user-configured folder wins when it exists and is writable.
  const QString configured = ui::AppSettings::instance().projects_directory();
  if (!configured.isEmpty()) {
    std::error_code ec;
    const std::filesystem::path candidate(configured.toStdString());
    if (std::filesystem::is_directory(candidate, ec)) {
      const auto probe = candidate / ".kreart_write_probe";
      QFile file(QString::fromStdString(probe.string()));
      if (file.open(QIODevice::WriteOnly)) {
        file.close();
        file.remove();
        return candidate;
      }
    }
  }

  // 2. Android: app-private storage, always writable.
  // 3. Desktop mobile preview: Documents/Kreart Projects.
  QString base;
#if defined(Q_OS_ANDROID)
  base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
  base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
  if (base.isEmpty()) {
    base = QDir::homePath();
  }
  const std::filesystem::path dir =
      std::filesystem::path(base.toStdString()) / "projects";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return dir;
}

#if defined(Q_OS_ANDROID)
namespace {

// Copies a content:// URI into the app cache and returns the local path.
QString copy_uri_to_cache(const QString &uri_string) {
  QJniObject uri = QJniObject::callStaticObjectMethod(
      "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
      QJniObject::fromString(uri_string).object<jstring>());
  if (!uri.isValid()) return {};

  QJniObject context = QNativeInterface::QAndroidApplication::context();
  QJniObject resolver = context.callObjectMethod(
      "getContentResolver", "()Landroid/content/ContentResolver;");
  if (!resolver.isValid()) return {};

  QJniObject stream = resolver.callObjectMethod(
      "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;", uri.object());
  if (!stream.isValid()) return {};

  QString cache_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_dir.isEmpty()) cache_dir = QDir::tempPath();
  const QString imports_dir = cache_dir + "/imports";
  QDir().mkpath(imports_dir);

  // Use the display name when the provider exposes one, else a numbered name.
  QJniObject display_name;
  QJniObject cursor = resolver.callObjectMethod(
      "query", "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
               "Ljava/lang/String;)Landroid/database/Cursor;",
      uri.object(), nullptr, nullptr, nullptr, nullptr);
  QString leaf;
  if (cursor.isValid()) {
    jboolean moved = cursor.callMethod<jboolean>("moveToFirst", "()Z");
    if (moved) {
      jint index = cursor.callMethod<jint>(
          "getColumnIndex", "(Ljava/lang/String;)I",
          QJniObject::fromString("_display_name").object<jstring>());
      if (index >= 0) {
        display_name = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", index);
        if (display_name.isValid()) leaf = display_name.toString();
      }
    }
    cursor.callMethod<void>("close", "()V");
  }
  if (leaf.isEmpty()) {
    leaf = QFileInfo(uri_string).fileName();
  }
  if (leaf.isEmpty()) leaf = "import.bin";

  QString target = imports_dir + "/" + leaf;
  int counter = 2;
  while (QFile::exists(target)) {
    const QFileInfo info(leaf);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    target = imports_dir + "/" + stem + " " + QString::number(counter) +
             (suffix.isEmpty() ? QString() : "." + suffix);
    ++counter;
  }

  QFile out(target);
  if (!out.open(QIODevice::WriteOnly)) return {};

  constexpr int kBufferSize = 64 * 1024;
  QJniEnvironment env;
  jbyteArray buffer = env->NewByteArray(kBufferSize);
  if (!buffer) return {};
  while (true) {
    const jint read = stream.callMethod<jint>("read", "([B)I", buffer);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }
    if (read <= 0) break;
    jbyte *bytes = env->GetByteArrayElements(buffer, nullptr);
    if (bytes) {
      out.write(reinterpret_cast<const char *>(bytes), read);
      env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    }
  }
  env->DeleteLocalRef(buffer);
  out.close();
  return target;
}

} // namespace

QString pick_file(QWidget *parent, const QString &mime, const QString &title) {
  Q_UNUSED(parent);
  Q_UNUSED(title);
  const QJniObject action = QJniObject::fromString("android.intent.action.OPEN_DOCUMENT");
  QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
  intent.callObjectMethod("addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                          QJniObject::fromString("android.intent.category.OPENABLE").object<jstring>());
  intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                          QJniObject::fromString("*/*").object<jstring>());

  QString result;
  QEventLoop loop;
  auto callback = [&result, &loop](int request_code, int result_code, const QJniObject &data) {
    if (result_code == -1 /* RESULT_OK */ && data.isValid()) {
      QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
      if (uri.isValid()) {
        result = copy_uri_to_cache(uri.toString());
      }
    }
    loop.quit();
  };

  QtAndroidPrivate::startActivity(intent, 0x4b52 /* KREART */, callback);
  loop.exec();
  return result;
}

QString pick_folder(QWidget *parent) {
  Q_UNUSED(parent);
  const QJniObject action = QJniObject::fromString("android.intent.action.OPEN_DOCUMENT_TREE");
  QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
  intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x00000040 /* GRANT_PERSISTABLE */);

  QString result;
  QEventLoop loop;
  auto callback = [&result, &loop](int, int result_code, const QJniObject &data) {
    if (result_code == -1 && data.isValid()) {
      QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
      if (uri.isValid()) {
        result = uri.toString();
      }
    }
    loop.quit();
  };
  QtAndroidPrivate::startActivity(intent, 0x4b53, callback);
  loop.exec();
  // Content-tree URIs are not filesystem paths; callers fall back to the
  // app-private projects directory when this does not resolve to a real path.
  if (!result.isEmpty() && !result.startsWith("file://")) {
    return {};
  }
  return result;
}

#else  // !Q_OS_ANDROID

QString pick_file(QWidget *parent, const QString &mime, const QString &title) {
  Q_UNUSED(parent);
  Q_UNUSED(mime);
  Q_UNUSED(title);
  // Desktop `-m` preview falls back to QFileDialog at the call site.
  return {};
}

QString pick_folder(QWidget *parent) {
  Q_UNUSED(parent);
  return {};
}

#endif

} // namespace kreart::mobile
