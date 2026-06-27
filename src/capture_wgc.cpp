#include "capture_wgc.h"

#include "logging.h"

#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/base.h>


#include <dxgi.h>

namespace {

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

winrt::com_ptr<ID3D11Texture2D>
getTextureFromSurface(IDirect3DSurface const &surface) {
  auto access = surface.as<
      ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
  winrt::com_ptr<ID3D11Texture2D> texture;
  check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
  return texture;
}

Direct3D11CaptureFramePool createFramePool(IDirect3DDevice const &device,
                                           uint32_t width, uint32_t height) {
  return Direct3D11CaptureFramePool::CreateFreeThreaded(
      device, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
      {static_cast<int32_t>(width), static_cast<int32_t>(height)});
}

} // namespace

struct WgcCapture::Impl {
  D3dDevice &device;
  HWND target;
  FrameCallback callback;
  IDirect3DDevice winrtDevice{nullptr};
  GraphicsCaptureItem item{nullptr};
  Direct3D11CaptureFramePool pool{nullptr};
  GraphicsCaptureSession session{nullptr};
  event_token frameToken{};
  event_token closedToken{};
  uint32_t width = 0;
  uint32_t height = 0;
  WgcCapture *owner = nullptr;

  Impl(D3dDevice &d, HWND t, WgcCapture *o) : device(d), target(t), owner(o) {}

  void recreatePool(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == 0 || newHeight == 0) {
      return;
    }
    width = newWidth;
    height = newHeight;
    owner->contentWidth_ = newWidth;
    owner->contentHeight_ = newHeight;

    if (session) {
      session.Close();
      session = nullptr;
    }
    if (pool) {
      pool.Close();
      pool = nullptr;
    }

    pool = createFramePool(winrtDevice, width, height);
    session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    session.StartCapture();
    logMessage(LogLevel::Verbose,
               "WGC frame pool recreated: " + std::to_string(width) + "x" +
                   std::to_string(height));
  }

  void onFrameArrived(Direct3D11CaptureFramePool const &sender,
                      winrt::Windows::Foundation::IInspectable const &) {
    const Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
    if (!frame) {
      return;
    }

    const auto size = frame.ContentSize();
    const uint32_t frameW = static_cast<uint32_t>(size.Width);
    const uint32_t frameH = static_cast<uint32_t>(size.Height);
    if (frameW == 0 || frameH == 0) {
      logMessage(LogLevel::Verbose,
                 "Received zero-size frame (window may be minimized)");
      return;
    }

    if (frameW != width || frameH != height) {
      recreatePool(frameW, frameH);
    }

    auto surface = frame.Surface();
    auto texture = getTextureFromSurface(surface);
    if (callback) {
      CapturedFrame captured{};
      captured.texture = texture.get();
      captured.width = frameW;
      captured.height = frameH;
      callback(captured);
    }
  }

  void onClosed(GraphicsCaptureItem const &,
                winrt::Windows::Foundation::IInspectable const &) {
    owner->targetClosed_.store(true);
    logMessage(LogLevel::Info, "Target window closed");
  }
};

Status checkCaptureSupport() {
  using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto rtlGetVersion =
      reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (rtlGetVersion) {
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) == 0 && version.dwBuildNumber < 18362) {
      return Status::fail(
          "wrec requires Windows 10 version 1903 (build 18362) or later.");
    }
  }

  if (!winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
          L"Windows.Graphics.Capture.GraphicsCaptureSession")) {
    return Status::fail(
        "Windows Graphics Capture is not available on this system.");
  }
  return Status::ok();
}

WgcCapture::WgcCapture(D3dDevice &device, HWND target)
    : device_(device), target_(target) {
  impl_ = new Impl(device_, target_, this);
}

WgcCapture::~WgcCapture() {
  stop();
  delete impl_;
  impl_ = nullptr;
}

Status WgcCapture::start(FrameCallback onFrame) {
  if (!IsWindow(target_)) {
    return Status::fail("Target HWND is not valid");
  }

  impl_->callback = std::move(onFrame);

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
  const HRESULT hrDxgi =
      device_.device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hrDxgi)) {
    return Status::fail("QueryInterface IDXGIDevice failed: " +
                        formatHresult(hrDxgi));
  }

  winrt::com_ptr<IInspectable> inspectable;
  const HRESULT hrCreate =
      CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
  if (FAILED(hrCreate)) {
    return Status::fail("CreateDirect3D11DeviceFromDXGIDevice failed: " +
                        formatHresult(hrCreate));
  }
  impl_->winrtDevice = inspectable.as<IDirect3DDevice>();

  auto interopFactory = get_activation_factory<GraphicsCaptureItem,
                                               IGraphicsCaptureItemInterop>();
  GraphicsCaptureItem item{nullptr};
  const HRESULT hrItem = interopFactory->CreateForWindow(
      target_, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item));
  if (FAILED(hrItem)) {
    return Status::fail("CreateForWindow failed: " + formatHresult(hrItem));
  }
  impl_->item = item;

  const auto initialSize = item.Size();
  impl_->recreatePool(static_cast<uint32_t>(initialSize.Width),
                      static_cast<uint32_t>(initialSize.Height));

  impl_->frameToken = impl_->pool.FrameArrived(
      [this](Direct3D11CaptureFramePool const &sender,
             winrt::Windows::Foundation::IInspectable const &args) {
        impl_->onFrameArrived(sender, args);
      });
  impl_->closedToken = impl_->item.Closed(
      [this](GraphicsCaptureItem const &sender,
             winrt::Windows::Foundation::IInspectable const &args) {
        impl_->onClosed(sender, args);
      });
  return Status::ok();
}

void WgcCapture::stop() {
  if (!impl_) {
    return;
  }
  if (impl_->pool && impl_->frameToken) {
    impl_->pool.FrameArrived(impl_->frameToken);
    impl_->frameToken = {};
  }
  if (impl_->item && impl_->closedToken) {
    impl_->item.Closed(impl_->closedToken);
    impl_->closedToken = {};
  }
  if (impl_->session) {
    impl_->session.Close();
    impl_->session = nullptr;
  }
  if (impl_->pool) {
    impl_->pool.Close();
    impl_->pool = nullptr;
  }
  impl_->item = nullptr;
  impl_->winrtDevice = nullptr;
}
