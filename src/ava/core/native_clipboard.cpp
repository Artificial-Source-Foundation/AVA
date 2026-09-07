#include "sys.h"
#include "ava/core/macos_clipboard_internal.h"
#include "ava/core/native_clipboard.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ava::core {
namespace {

#ifdef __APPLE__
bool environment_present(char const* name)
{
  auto const* value = std::getenv(name);
  return value && *value;
}

struct ReleaseCf
{
  void operator()(CFTypeRef value) const
  {
    if (value)
      CFRelease(value);
  }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

template <typename T>
using CfOwner = std::unique_ptr<std::remove_pointer_t<T>, ReleaseCf>;

Error pasteboard_error(std::string message, OSStatus status)
{
  auto error = Error(ErrorCategory::Io, std::move(message));
  error.with_context("os_status", std::to_string(status));
  return error;
}

// Pasteboard Services is not thread safe. This mutex owns serialization of all
// AVA pasteboard transactions, including image reads and text copies.
std::mutex& pasteboard_mutex()
{
  static std::mutex mutex;
  return mutex;
}

Result<CfOwner<PasteboardRef>> open_clipboard()
{
  PasteboardRef raw = nullptr;
  auto const status = PasteboardCreate(kPasteboardClipboard, &raw);
  CfOwner<PasteboardRef> pasteboard(raw);
  if (status != noErr || !pasteboard)
    return std::unexpected(pasteboard_error("could not open the macOS clipboard", status));
  return pasteboard;
}

Result<std::string> tiff_to_png(CFDataRef data, std::size_t max_bytes)
{
  CfOwner<CGImageSourceRef> source(CGImageSourceCreateWithData(data, nullptr));
  if (!source)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "clipboard TIFF image is invalid"));
  CfOwner<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.get(), 0, nullptr));
  std::int64_t width = 0;
  std::int64_t height = 0;
  auto dimension = [&](CFStringRef key, std::int64_t& value) {
    auto const number = properties ? CFDictionaryGetValue(properties.get(), key) : nullptr;
    return number && CFGetTypeID(number) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(number), kCFNumberSInt64Type, &value);
  };
  // Bound decoded memory before asking ImageIO to decode a clipboard promise.
  constexpr std::int64_t kMaxPixels = 32 * 1024 * 1024;
  if (!dimension(kCGImagePropertyPixelWidth, width) || !dimension(kCGImagePropertyPixelHeight, height) || width <= 0 || height <= 0 ||
      width > kMaxPixels / height)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "clipboard image dimensions exceed the supported limit"));
  CfOwner<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr));
  CfOwner<CFMutableDataRef> png(CFDataCreateMutable(kCFAllocatorDefault, 0));
  if (!image || !png)
    return std::unexpected(Error(ErrorCategory::Io, "could not decode the clipboard image"));
  CfOwner<CGImageDestinationRef> destination(CGImageDestinationCreateWithData(png.get(), CFSTR("public.png"), 1, nullptr));
  if (!destination)
    return std::unexpected(Error(ErrorCategory::Io, "could not create clipboard PNG data"));
  CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
  if (!CGImageDestinationFinalize(destination.get()))
    return std::unexpected(Error(ErrorCategory::Io, "could not encode the clipboard image as PNG"));
  auto const size = static_cast<std::size_t>(CFDataGetLength(png.get()));
  if (size > max_bytes)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "clipboard image is too large"));
  return std::string(reinterpret_cast<char const*>(CFDataGetBytePtr(png.get())), size);
}
#endif

}  // namespace

bool native_clipboard_enabled()
{
#ifdef __APPLE__
  auto const* backend = std::getenv("AVA_CLIPBOARD_BACKEND");
  return !(backend && std::string_view(backend) == "terminal") && !environment_present("SSH_CONNECTION") && !environment_present("SSH_CLIENT") &&
         !environment_present("SSH_TTY");
#else
  return false;
#endif
}

VoidResult write_native_clipboard_text(std::string_view text)
{
#ifdef __APPLE__
  // A clipboard owner can delay an image promise. A copy action on the UI
  // thread must never wait behind that background read.
  std::unique_lock lock(pasteboard_mutex(), std::try_to_lock);
  if (!lock.owns_lock())
    return std::unexpected(Error(ErrorCategory::Io, "macOS clipboard is busy; try copying again after the image paste"));
  auto pasteboard = open_clipboard();
  if (!pasteboard)
    return std::unexpected(std::move(pasteboard.error()));
  return macos_clipboard::write_text(pasteboard->get(), text);
#else
  static_cast<void>(text);
  return std::unexpected(Error(ErrorCategory::Configuration, "native clipboard is unavailable on this platform"));
#endif
}

Result<std::optional<std::string>> read_native_clipboard_image(std::size_t max_bytes)
{
#ifdef __APPLE__
  std::lock_guard lock(pasteboard_mutex());
  auto pasteboard = open_clipboard();
  if (!pasteboard)
    return std::unexpected(std::move(pasteboard.error()));
  return macos_clipboard::read_image(pasteboard->get(), max_bytes);
#else
  static_cast<void>(max_bytes);
  return std::optional<std::string>{};
#endif
}

#ifdef __APPLE__
namespace macos_clipboard {

VoidResult write_text(PasteboardRef pasteboard, std::string_view text)
{
  if (!pasteboard || text.empty() || text.size() > kMaxNativeClipboardTextBytes)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "clipboard text is empty or exceeds the 16 MiB limit"));
  CfOwner<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<UInt8 const*>(text.data()), static_cast<CFIndex>(text.size())));
  if (!data)
    return std::unexpected(Error(ErrorCategory::Io, "could not allocate clipboard text"));
  static_cast<void>(PasteboardSynchronize(pasteboard));
  auto status = PasteboardClear(pasteboard);
  if (status == noErr)
    status = PasteboardPutItemFlavor(pasteboard, reinterpret_cast<PasteboardItemID>(1), CFSTR("public.utf8-plain-text"), data.get(), 0);
  if (status != noErr)
    return std::unexpected(pasteboard_error("could not write the macOS clipboard", status));
  return {};
}

Result<std::optional<std::string>> read_image(PasteboardRef pasteboard, std::size_t max_bytes)
{
  if (!pasteboard || max_bytes == 0)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "invalid clipboard image read"));
  static_cast<void>(PasteboardSynchronize(pasteboard));
  ItemCount count = 0;
  auto status = PasteboardGetItemCount(pasteboard, &count);
  if (status != noErr)
    return std::unexpected(pasteboard_error("could not inspect the macOS clipboard", status));
  // Ordinary clipboard images contain one item; keep foreign multi-item boards bounded.
  for (ItemCount index = 1; index <= count && index <= 32; ++index)
  {
    PasteboardItemID item = nullptr;
    status = PasteboardGetItemIdentifier(pasteboard, index, &item);
    if (status != noErr)
      return std::unexpected(pasteboard_error("could not inspect a clipboard item", status));
    for (auto flavor : {CFSTR("public.png"), CFSTR("public.jpeg"), CFSTR("org.webmproject.webp"), CFSTR("com.compuserve.gif"), CFSTR("public.tiff")})
    {
      PasteboardFlavorFlags flags = 0;
      if (PasteboardGetItemFlavorFlags(pasteboard, item, flavor, &flags) != noErr)
        continue;
      CFDataRef raw = nullptr;
      status = PasteboardCopyItemFlavorData(pasteboard, item, flavor, &raw);
      CfOwner<CFDataRef> data(raw);
      if (status != noErr || !data)
        return std::unexpected(pasteboard_error("could not read the clipboard image", status));
      auto const size = static_cast<std::size_t>(CFDataGetLength(data.get()));
      if (size == 0)
        continue;
      if (size > max_bytes)
        return std::unexpected(Error(ErrorCategory::InvalidArgument, "clipboard image is too large"));
      if (CFEqual(flavor, CFSTR("public.tiff")))
      {
        auto png = tiff_to_png(data.get(), max_bytes);
        if (!png)
          return std::unexpected(std::move(png.error()));
        return std::optional<std::string>(std::move(*png));
      }
      return std::optional<std::string>(std::in_place, reinterpret_cast<char const*>(CFDataGetBytePtr(data.get())), size);
    }
  }
  return std::optional<std::string>{};
}

}  // namespace macos_clipboard
#endif
}  // namespace ava::core
