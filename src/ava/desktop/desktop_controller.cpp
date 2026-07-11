#include "desktop_controller.h"

#include <algorithm>

namespace ava::desktop {

DesktopController::DesktopController(QObject* parent) : QObject(parent), active_session_(QStringLiteral("AVA Desktop Shell"))
{
  sessions_ =
      QStringList{QStringLiteral("AVA Desktop Shell"), QStringLiteral("Qt Quick Spike"), QStringLiteral("Backend RPC Bridge"), QStringLiteral("Permission UX")};
  transcript_ = QStringList{
      QStringLiteral("System: Qt Quick prototype loaded. The desktop UI is running in QML with a C++ controller behind it."),
      QStringLiteral("AVA: Send a message to exercise the QML to C++ callback and mock streaming response."),
  };

  stream_timer_.setInterval(18);
  connect(&stream_timer_, &QTimer::timeout, this, &DesktopController::advanceStreamingResponse);
}

QString DesktopController::activeSession() const
{
  return active_session_;
}

QStringList DesktopController::sessions() const
{
  return sessions_;
}

QStringList DesktopController::transcript() const
{
  return transcript_;
}

QString DesktopController::streamingText() const
{
  return streaming_text_;
}

bool DesktopController::permissionVisible() const
{
  return permission_visible_;
}

bool DesktopController::commandPaletteVisible() const
{
  return command_palette_visible_;
}

void DesktopController::switchSession(QString const& session_name)
{
  if (session_name == active_session_ || !sessions_.contains(session_name))
    return;

  finishStreamingResponse();
  active_session_ = session_name;
  transcript_ = QStringList{QStringLiteral("System: Switched to %1.").arg(active_session_),
                            QStringLiteral("AVA: Session-backed transcript loading is the next backend bridge step.")};
  setPermissionVisible(active_session_ == QStringLiteral("Permission UX"));

  emit activeSessionChanged();
  emit transcriptChanged();
}

void DesktopController::sendMessage(QString const& message)
{
  auto const trimmed = message.trimmed();
  if (trimmed.isEmpty())
    return;

  finishStreamingResponse();
  transcript_.append(QStringLiteral("You: %1").arg(trimmed));
  emit transcriptChanged();

  pending_response_ = QStringLiteral(
                          "QML delivered this message into a C++ QObject. Next, this controller can call AVA's session/provider runtime or the RPC bridge "
                          "while QML stays focused on layout, input, and rendering. Received: %1")
                          .arg(trimmed);
  streaming_text_.clear();
  stream_index_ = 0;
  emit streamingTextChanged();

  setPermissionVisible(false);
  setCommandPaletteVisible(false);
  stream_timer_.start();
}

void DesktopController::approvePermission()
{
  transcript_.append(QStringLiteral("System: Permission approved in the desktop shell prototype."));
  emit transcriptChanged();
  setPermissionVisible(false);
}

void DesktopController::denyPermission()
{
  transcript_.append(QStringLiteral("System: Permission denied in the desktop shell prototype."));
  emit transcriptChanged();
  setPermissionVisible(false);
}

void DesktopController::showCommandPalette()
{
  setCommandPaletteVisible(true);
}

void DesktopController::hideCommandPalette()
{
  setCommandPaletteVisible(false);
}

void DesktopController::advanceStreamingResponse()
{
  if (stream_index_ >= pending_response_.size())
  {
    finishStreamingResponse();
    setPermissionVisible(true);
    return;
  }

  auto const remaining = pending_response_.size() - stream_index_;
  auto const chunk_size = std::min<qsizetype>(4, remaining);
  streaming_text_.append(pending_response_.mid(stream_index_, chunk_size));
  stream_index_ += chunk_size;
  emit streamingTextChanged();
}

void DesktopController::finishStreamingResponse()
{
  if (stream_timer_.isActive())
    stream_timer_.stop();

  if (!streaming_text_.isEmpty())
  {
    transcript_.append(QStringLiteral("AVA: %1").arg(streaming_text_));
    streaming_text_.clear();
    emit transcriptChanged();
    emit streamingTextChanged();
  }

  pending_response_.clear();
  stream_index_ = 0;
}

void DesktopController::setPermissionVisible(bool visible)
{
  if (permission_visible_ == visible)
    return;
  permission_visible_ = visible;
  emit permissionVisibleChanged();
}

void DesktopController::setCommandPaletteVisible(bool visible)
{
  if (command_palette_visible_ == visible)
    return;
  command_palette_visible_ = visible;
  emit commandPaletteVisibleChanged();
}

}  // namespace ava::desktop
