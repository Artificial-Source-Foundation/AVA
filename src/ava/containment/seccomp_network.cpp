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
constexpr bool kIsX86_64 = true;
#elif defined(__aarch64__)
constexpr std::uint32_t kNativeAuditArch = AUDIT_ARCH_AARCH64;
constexpr bool kIsX86_64 = false;
#else
// On unsupported architectures the filter is not built; the plan reports
// Unavailable and callers downgrade to Ask.
constexpr std::uint32_t kNativeAuditArch = 0;
constexpr bool kIsX86_64 = false;
#endif

// io_uring_setup has a stable syscall number (425) on both supported
// architectures. Define it unconditionally so the filter always blocks it,
// even when the installed kernel headers predate io_uring. A static assertion
// verifies the header constant matches when both are available.
constexpr std::uint32_t kStableIoUringSetupSyscall = 425;

#ifdef __NR_io_uring_setup
static_assert(static_cast<std::uint32_t>(__NR_io_uring_setup) == kStableIoUringSetupSyscall,
              "stable io_uring_setup syscall number must match the kernel header");
constexpr std::uint32_t kIoUringSetupSyscall = static_cast<std::uint32_t>(__NR_io_uring_setup);
#else
constexpr std::uint32_t kIoUringSetupSyscall = kStableIoUringSetupSyscall;
#endif

// On x86_64, the x32 ABI shares the AUDIT_ARCH_X86_64 architecture but uses
// syscall numbers with __X32_SYSCALL_BIT (0x40000000) set. Rejecting any
// syscall number with this bit prevents x32 bypass of the filter.
#if defined(__x86_64__) && defined(__X32_SYSCALL_BIT)
constexpr std::uint32_t kX32SyscallBit = __X32_SYSCALL_BIT;
#else
constexpr std::uint32_t kX32SyscallBit = 0;
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
  // The filter blocks all network-related syscall paths so no externally
  // connected descriptor can be acquired or used:
  //
  // 1. Kill any process whose architecture is not the native one (fail closed
  //    against x32 or cross-arch bypasses).
  // 2. On x86_64, kill any syscall number with __X32_SYSCALL_BIT set.
  // 3. Block socket(), socketpair(), connect(), sendto(), sendmsg(),
  //    sendmmsg(), and io_uring_setup() with EPERM.
  // 4. Allow all other syscalls.
  //
  // Blocking socket/socketpair prevents new socket creation (including
  // AF_UNIX/abstract). Blocking connect/sendto/sendmsg/sendmmsg prevents
  // using any inherited (though closed) or otherwise acquired descriptor.
  // Inherited non-stdio FDs are closed before this filter is installed.

  std::vector<BpfStmt> program;

  // [0] Load architecture.
  program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)));

  // [1] If arch == native, skip the kill instruction. Otherwise fall through.
  program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kNativeAuditArch, 1, 0));

  // [2] Kill: non-native architecture.
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

  // [3] Load syscall number.
  program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));

  if constexpr (kIsX86_64 && kX32SyscallBit != 0)
  {
    // [4] AND syscall number with __X32_SYSCALL_BIT. If the result is
    //     non-zero, the syscall is an x32 call; kill it.
    program.push_back(AVA_BPF_STMT(BPF_ALU | BPF_AND | BPF_K, kX32SyscallBit));

    // [5] If result != 0 (x32 bit set), jump to kill. Otherwise fall through.
    //     The kill target is the last instruction (KILL_PROCESS).
    program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0));

    // [6] Kill: x32 syscall.
    program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

    // [7] Reload syscall number (the AND clobbered the accumulator).
    program.push_back(AVA_BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)));
  }

  // Blocked syscall numbers. Each check jumps to the EPERM block if matched.
  // Using the __NR_ constants from headers for fundamental syscalls that have
  // been stable for decades; io_uring_setup uses the verified stable constant.
  struct BlockedSyscall
  {
    std::uint32_t nr;
    char const* name;
  };
  // The following are fundamental networking syscalls defined on all supported
  // architectures. They are present in every Linux kernel header set AVA targets.
  BlockedSyscall const blocked[] = {
      {static_cast<std::uint32_t>(__NR_socket), "socket"},
      {static_cast<std::uint32_t>(__NR_socketpair), "socketpair"},
      {static_cast<std::uint32_t>(__NR_connect), "connect"},
      {static_cast<std::uint32_t>(__NR_sendto), "sendto"},
      {static_cast<std::uint32_t>(__NR_sendmsg), "sendmsg"},
#ifdef __NR_sendmmsg
      {static_cast<std::uint32_t>(__NR_sendmmsg), "sendmmsg"},
#endif
      {kIoUringSetupSyscall, "io_uring_setup"},
  };

  // Record the index where the EPERM block will be. We'll set the jump
  // targets after we know the final layout.
  std::size_t const first_check_index = program.size();
  for (auto const& entry : blocked)
  {
    // JEQ: if nr == blocked_nr, jump to EPERM (we'll fix the target later),
    // otherwise fall through to next check.
    program.push_back(AVA_BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, entry.nr, 0, 0));
    static_cast<void>(entry.name);
  }

  // Allow-all target: if no blocked syscall matched.
  std::size_t const allow_index = program.size();
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

  // EPERM block target.
  std::size_t const eperm_index = program.size();
  program.push_back(AVA_BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)));

  // Fix up the jump targets: each blocked-syscall check jumps to EPERM on
  // match (jt), falls through on no-match (jf=0, next instruction).
  for (std::size_t i = first_check_index; i < allow_index; ++i)
  {
    program[i].jt = static_cast<std::uint8_t>(eperm_index - i - 1);
  }

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
  if (kNativeAuditArch == 0)
    return false;

  // Non-mutating probe: verify the kernel supports SECCOMP_RET_ERRNO via
  // SECCOMP_GET_ACTION_AVAIL. This avoids installing a filter that the kernel
  // silently ignores or rejects at exec time.
  std::uint32_t const action = SECCOMP_RET_ERRNO;
  long const result = ::syscall(SYS_seccomp, SECCOMP_GET_ACTION_AVAIL, 0u, &action);
  if (result != 0)
  {
    // Fallback: check /proc/self/status for seccomp support. If the kernel
    // doesn't support SECCOMP_GET_ACTION_AVAIL (pre-4.14), we still need to
    // determine whether seccomp filters are available at all.
    // PR_GET_SECCOMP returns 0 (disabled), 1 (strict), or 2 (filter).
    errno = 0;
    if (::prctl(PR_GET_SECCOMP) < 0 && errno == EINVAL)
      return false;
    // The kernel supports seccomp; assume SECCOMP_RET_ERRNO is available
    // (it has been since seccomp filter mode was introduced in 3.5).
  }
  return true;
}

ava::core::VoidResult apply_seccomp_network_filter()
{
  if (!seccomp_network_filter_supported())
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "seccomp network filter is not supported on this architecture or kernel"));

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
