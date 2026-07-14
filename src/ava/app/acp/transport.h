#pragma once

#include "ava/app/acp/protocol.h"

#include <memory>
#include <string>

namespace ava::app::acp {

enum class ReadRecordStatus
{
  Record,
  EndOfFile,
  RecoverableError,
  FatalError,
  Canceled,
};

struct ReadRecord
{
  ReadRecordStatus status = ReadRecordStatus::FatalError;
  std::string record;
  std::string diagnostic;
  // Set for discarded oversized records from a constant-memory top-level scan.
  EnvelopeIntent intent = EnvelopeIntent::Unknown;
};

class RecordTransport
{
 public:
  virtual ~RecordTransport() = default;
  [[nodiscard]] virtual ReadRecord read_record() = 0;
  [[nodiscard]] virtual ava::core::VoidResult write_record(std::string const& record) = 0;
  virtual void cancel() noexcept = 0;
};

// The returned transport borrows input_fd and output_fd. Its wake descriptors and
// temporary O_NONBLOCK output setting are owned and restored with RAII.
[[nodiscard]] ava::core::Result<std::unique_ptr<RecordTransport>> make_fd_record_transport(int input_fd, int output_fd);

}  // namespace ava::app::acp
