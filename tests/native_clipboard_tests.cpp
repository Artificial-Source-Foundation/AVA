#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/core/macos_clipboard_internal.h"
#include "ava/core/native_clipboard.h"

#include <array>
#include <memory>
#include <string>
#include <type_traits>

#ifdef __APPLE__
namespace {
struct ReleaseCf
{
  void operator()(CFTypeRef value) const
  {
    if (value)
      CFRelease(value);
  }
};
template <typename T>
using CfOwner = std::unique_ptr<std::remove_pointer_t<T>, ReleaseCf>;

std::string copy_text(PasteboardRef pasteboard)
{
  CFDataRef raw = nullptr;
  auto const status = PasteboardCopyItemFlavorData(pasteboard, reinterpret_cast<PasteboardItemID>(1), CFSTR("public.utf8-plain-text"), &raw);
  CfOwner<CFDataRef> data(raw);
  if (status != noErr || !data)
    return {};
  return std::string(reinterpret_cast<char const*>(CFDataGetBytePtr(data.get())), static_cast<std::size_t>(CFDataGetLength(data.get())));
}

std::string one_pixel_tiff()
{
  std::array<UInt8, 4> pixel{10, 20, 30, 255};
  CfOwner<CGDataProviderRef> provider(CGDataProviderCreateWithData(nullptr, pixel.data(), pixel.size(), nullptr));
  CfOwner<CGColorSpaceRef> color(CGColorSpaceCreateDeviceRGB());
  CfOwner<CGImageRef> image(
      CGImageCreate(1, 1, 8, 32, 4, color.get(), kCGImageAlphaPremultipliedLast, provider.get(), nullptr, false, kCGRenderingIntentDefault));
  CfOwner<CFMutableDataRef> data(CFDataCreateMutable(kCFAllocatorDefault, 0));
  if (!image || !data)
    return {};
  CfOwner<CGImageDestinationRef> destination(CGImageDestinationCreateWithData(data.get(), CFSTR("public.tiff"), 1, nullptr));
  if (!destination)
    return {};
  CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
  if (!CGImageDestinationFinalize(destination.get()))
    return {};
  return std::string(reinterpret_cast<char const*>(CFDataGetBytePtr(data.get())), static_cast<std::size_t>(CFDataGetLength(data.get())));
}

bool put_image(PasteboardRef pasteboard, CFStringRef flavor, std::string const& bytes)
{
  CfOwner<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<UInt8 const*>(bytes.data()), static_cast<CFIndex>(bytes.size())));
  return data && PasteboardClear(pasteboard) == noErr &&
         PasteboardPutItemFlavor(pasteboard, reinterpret_cast<PasteboardItemID>(1), flavor, data.get(), 0) == noErr;
}
}  // namespace
#endif

void run_native_clipboard_tests()
{
  ScopedEnvVar terminal("AVA_CLIPBOARD_BACKEND", "terminal");
  expect(!ava::core::native_clipboard_enabled(), "terminal override disables native clipboard access");
#ifdef __APPLE__
  ScopedEnvVar automatic("AVA_CLIPBOARD_BACKEND", "auto");
  ScopedEnvVar ssh_connection("SSH_CONNECTION", "");
  ScopedEnvVar ssh_client("SSH_CLIENT", "");
  ScopedEnvVar ssh_tty("SSH_TTY", "");
  expect(ava::core::native_clipboard_enabled(), "local Mac sessions select the native clipboard");
  {
    ScopedEnvVar remote("SSH_CONNECTION", "synthetic-remote");
    expect(!ava::core::native_clipboard_enabled(), "SSH sessions leave copying to the client terminal");
  }

  PasteboardRef raw = nullptr;
  auto const created = PasteboardCreate(kPasteboardUniqueName, &raw);
  CfOwner<PasteboardRef> pasteboard(raw);
  expect(created == noErr && pasteboard, "create an isolated native pasteboard without accessing the user's clipboard");
  if (!pasteboard)
    return;
  auto const initial_image = ava::core::macos_clipboard::read_image(pasteboard.get(), 1024);
  expect(initial_image && !*initial_image, "empty native pasteboard has no image");
  auto text = std::string("# Markdown\n\nCafé · 日本語 😀\n\n```cpp\nint main() {}\n```\n");
  text.append(70'000, 'x');
  auto copied = ava::core::macos_clipboard::write_text(pasteboard.get(), text);
  expect(copied && copy_text(pasteboard.get()) == text, "native clipboard preserves UTF-8 Markdown larger than the OSC52 limit");
  auto empty = ava::core::macos_clipboard::write_text(pasteboard.get(), {});
  auto oversized = ava::core::macos_clipboard::write_text(pasteboard.get(), std::string(ava::core::kMaxNativeClipboardTextBytes + 1, 'x'));
  expect(!empty && !oversized && copy_text(pasteboard.get()) == text, "invalid native copies preserve the existing clipboard content");
  auto const text_image = ava::core::macos_clipboard::read_image(pasteboard.get(), 1024);
  expect(text_image && !*text_image, "native image paste ignores plain text");

  auto const png = std::string("\x89PNG\r\n\x1a\n", 8) + "synthetic-session-image";
  expect(put_image(pasteboard.get(), CFSTR("public.png"), png), "seed a private PNG pasteboard item");
  auto image = ava::core::macos_clipboard::read_image(pasteboard.get(), png.size());
  expect(image && *image && **image == png, "native PNG paste returns exact image bytes for session validation");
  auto too_large = ava::core::macos_clipboard::read_image(pasteboard.get(), png.size() - 1);
  expect(!too_large, "native image paste enforces the attachment byte limit without truncation");

  auto const tiff = one_pixel_tiff();
  expect(!tiff.empty() && put_image(pasteboard.get(), CFSTR("public.tiff"), tiff), "seed a private TIFF image as supplied by Mac applications");
  auto converted = ava::core::macos_clipboard::read_image(pasteboard.get(), 1024 * 1024);
  expect(converted && *converted && (**converted).starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8)), "native TIFF paste converts to session-supported PNG");
  if (converted && *converted)
  {
    CfOwner<CFDataRef> data(
        CFDataCreate(kCFAllocatorDefault, reinterpret_cast<UInt8 const*>((**converted).data()), static_cast<CFIndex>((**converted).size())));
    CfOwner<CGImageSourceRef> source(CGImageSourceCreateWithData(data.get(), nullptr));
    CfOwner<CGImageRef> decoded(source ? CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr) : nullptr);
    expect(decoded && CGImageGetWidth(decoded.get()) == 1 && CGImageGetHeight(decoded.get()) == 1,
           "converted clipboard PNG decodes to the original image dimensions");
  }
#else
  expect(!ava::core::native_clipboard_enabled(), "non-Mac builds retain their terminal clipboard backend");
#endif
}
