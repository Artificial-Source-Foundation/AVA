#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace ava::desktop {

class DesktopController final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString activeSession READ activeSession NOTIFY activeSessionChanged)
  Q_PROPERTY(QStringList sessions READ sessions NOTIFY sessionsChanged)
  Q_PROPERTY(QStringList transcript READ transcript NOTIFY transcriptChanged)
  Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)
  Q_PROPERTY(bool permissionVisible READ permissionVisible NOTIFY permissionVisibleChanged)
  Q_PROPERTY(bool commandPaletteVisible READ commandPaletteVisible NOTIFY commandPaletteVisibleChanged)

 public:
  explicit DesktopController(QObject* parent = nullptr);

  [[nodiscard]] QString activeSession() const;
  [[nodiscard]] QStringList sessions() const;
  [[nodiscard]] QStringList transcript() const;
  [[nodiscard]] QString streamingText() const;
  [[nodiscard]] bool permissionVisible() const;
  [[nodiscard]] bool commandPaletteVisible() const;

  Q_INVOKABLE void switchSession(QString const& session_name);
  Q_INVOKABLE void sendMessage(QString const& message);
  Q_INVOKABLE void approvePermission();
  Q_INVOKABLE void denyPermission();
  Q_INVOKABLE void showCommandPalette();
  Q_INVOKABLE void hideCommandPalette();

  AVA_DEBUG_PRINT_MEMBERS_ON

 signals:
  void activeSessionChanged();
  void sessionsChanged();
  void transcriptChanged();
  void streamingTextChanged();
  void permissionVisibleChanged();
  void commandPaletteVisibleChanged();

 private slots:
  void advanceStreamingResponse();

 private:
  void finishStreamingResponse();
  void setPermissionVisible(bool visible);
  void setCommandPaletteVisible(bool visible);

  QString active_session_;
  QStringList sessions_;
  QStringList transcript_;
  QString streaming_text_;
  QString pending_response_;
  QTimer stream_timer_;
  qsizetype stream_index_ = 0;
  bool permission_visible_ = true;
  bool command_palette_visible_ = false;
};

}  // namespace ava::desktop
