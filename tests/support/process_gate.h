#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ava::test {

// Multiplex up to 64 persistent, one-shot process gates over one connected Unix socket.
//
// The constructor takes ownership of control_fd. open makes gate available to the peer, while wait blocks until the peer
// opens gate or timeout expires. Opens received in any order are retained, repeated opens and waits are idempotent, and
// peer exit before a requested open is an error. Calls on one endpoint must be serialized by its owner.
class ProcessGateSet
{
 public:
  explicit ProcessGateSet(int control_fd);
  ProcessGateSet(ProcessGateSet const&) = delete;
  ProcessGateSet& operator=(ProcessGateSet const&) = delete;
  ProcessGateSet(ProcessGateSet&& other) noexcept;
  ProcessGateSet& operator=(ProcessGateSet&& other) noexcept;
  ~ProcessGateSet();

  // Permanently open gate for the peer, where gate is in the range [0, 63].
  //
  // Repeated calls for an already-opened gate are cheap and send no duplicate message. Throws std::runtime_error when
  // the gate number is invalid or the peer control socket can no longer accept the open.
  void open(int gate);

  // Wait for the peer to open gate, where gate is in the range [0, 63], for at most timeout.
  //
  // Opens for other gates are cached. Returns immediately after a prior open was received and throws std::runtime_error
  // on timeout, invalid protocol data, socket failure, or peer exit before the requested gate opens.
  void wait(int gate, std::chrono::milliseconds timeout);

 private:
  void close() noexcept;

  int control_fd_ = -1;
  std::uint64_t opened_by_peer_ = 0;
  std::uint64_t opened_for_peer_ = 0;
};

}  // namespace ava::test
