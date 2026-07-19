#include "sys.h"
#include "ava/containment/containment.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef AF_INET
#include <sys/socket.h>
#endif

namespace ava::containment {
namespace {

// Detect the native audit architecture at compile time. The seccomp filter
// checks the architecture of each syscall so a 64-bit filter cannot be bypassed
// by a 32-bit syscall (x32 ABI on x86_64).
#if defined(__x86_64__)
constexpr std::uint32_t kNativeAuditArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr std::uint32_t kNativeAuditArch = AUDIT_ARCH_AARCH64;
#else
// On unsupported architectures the filter is not built; the plan reports
// Unavailable and callers downgrade to Ask.
constexpr std::uint32_t kNativeAuditArch = 0;
#endif

// BPF instruction helpers.
struct BpfStmt
{
  std::uint16_t code;
  std::uint8_t jt;
  std::uint8_t jf;
  std::uint32_t k;
};

#define AVA_BPF_STMT(code, k)                 \
  BpfStmt                                     \
  {                                           \
    static_cast<std::uint16_t>(code), 0, 0, k \
  }

#define AVA_BPF_JUMP(code, k, jt, jf)                                                                 \
  BpfStmt                                                                                             \
  {                                                                                                   \
    static_cast<std::uint16_t>(code), static_cast<std::uint8_t>(jt), static_cast<std::uint8_t>(jf), k \
  }

[[nodiscard]] std::vector<sock_filter> build_network_filter()
{
  // The filter:
  // 1. Kill any process whose architecture is not the native one (fail closed
  //    against x32 or cross-arch bypasses).
  // 2. Block socket(AF_INET) and socket(AF_INET6) with EPERM.
  // 3. Block io_uring_setup with EPERM (prevents io_uring bypass of the
  //    socket restriction).
  // 4. Allow all other syscalls.
  //
  // Since inherited non-stdio FDs are closed before this filter is installed,
  // there are no pre-existing IP sockets to connect/send through. Blocking
  // socket creation for IP domains is sufficient to prevent outbound network
  // paths. AF_UNIX (local IPC) remains allowed.

  std::vector<BpfStmt> program;

  // [0] Load architecture.
  program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));

  // [1] If arch == native, skip the kill instruction. Otherwise fall through.
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kNativeAuditArch, 1, 0));

  // [2] Kill: non-native architecture.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

  // [3] Load syscall number.
  program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));

  // [4] Is it socket()? If yes, continue to domain check. If no, skip to
  //     io_uring check. The jump distance is adjusted below.
  std::size_t const socket_check_index = program.size();
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 0));  // jt/jf filled later

  // [5] Load arg0 (socket domain).
  program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])));

  // [6] Is it AF_INET (2)? If yes, skip to block. If no, check AF_INET6.
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET, 2, 0));

  // [7] Is it AF_INET6 (10)? If yes, skip to block. If no, allow socket.
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, 1, 0));

  // [8] Allow socket (other domains like AF_UNIX).
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

  // [9] Block: return EPERM.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));

  // Now we know the distance from socket_check to io_uring check.
  // The socket_check jump: if socket, jt=0 (continue to [5]); if not, jf=?
  std::size_t const after_socket_block_index = program.size();
  program[socket_check_index].jf = static_cast<std::uint8_t>(after_socket_block_index - socket_check_index - 1);

#ifdef __NR_io_uring_setup
  // [10] Is it io_uring_setup? If yes, skip to block. If no, skip to allow.
  std::size_t const iouring_check_index = program.size();
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup, 0, 0));  // jt/jf filled later

  // [11] Block: return EPERM.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));

  // [12] Allow everything else.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

  // Fix up io_uring jump: if io_uring_setup, jt=0 (continue to [11] block);
  // if not, jf=1 (skip [11] to [12] allow).
  program[iouring_check_index].jf = 1;
#else
  // No io_uring syscall on this kernel/arch; just allow everything else.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
#endif

  // Convert to sock_filter array.
  std::vector<sock_filter> filter;
  filter.reserve(program.size());
  for (auto const& stmt : program)
  {
    sock_filter f{};
    f.code = stmt.code;
    f.jt = stmt.jt;
    f.jf = stmt.jf;
    f.k = stmt.k;
    filter.push_back(f);
  }
  return filter;
}

}  // namespace

bool seccomp_network_filter_supported() noexcept
{
  return kNativeAuditArch != 0;
}

ava::core::VoidResult apply_seccomp_network_filter()
{
  if (!seccomp_network_filter_supported())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "seccomp network filter is not supported on this architecture"));

  // no_new_privs must be set before installing a seccomp filter. It is
  // inherited across execve and prevents the child from gaining privileges
  // through setuid binaries.
  if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "failed to set PR_SET_NO_NEW_PRIVS before seccomp"));

  auto filter = build_network_filter();
  if (filter.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "seccomp network filter program is empty"));

  struct sock_fprog prog{};
  prog.len = static_cast<std::uint16_t>(filter.size());
  prog.filter = filter.data();

  if (::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0u, &prog) != 0)
  {
    // Fall back to prctl if seccomp(2) is unavailable (older kernels).
    if (::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "failed to install seccomp network filter"));
  }
  return {};
}

ava::core::VoidResult apply_no_new_privs()
{
  if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "failed to set PR_SET_NO_NEW_PRIVS"));
  return {};
}

}  // namespace ava::containment
