#include "mf_encoder.h"

#include "logging.h"

#include <mferror.h>
#include <wrl/client.h>

#include <cstring>

namespace {

using Microsoft::WRL::ComPtr;

Status setAttributeSize(IMFMediaType *type, uint32_t width, uint32_t height) {
  const HRESULT hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
  if (FAILED(hr)) {
    return Status::fail("MFSetAttributeSize failed: " + formatHresult(hr));
  }
  return Status::ok();
}

Status setAttributeRatio(IMFMediaType *type, REFGUID key, uint32_t num,
                         uint32_t den) {
  const HRESULT hr = MFSetAttributeRatio(type, key, num, den);
  if (FAILED(hr)) {
    return Status::fail("MFSetAttributeRatio failed: " + formatHresult(hr));
  }
  return Status::ok();
}

} // namespace

MfStartupGuard::MfStartupGuard() {
  ok_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
}

MfStartupGuard::~MfStartupGuard() {
  if (ok_) {
    MFShutdown();
  }
}

Status MfEncoder::open(const std::wstring &path, uint32_t width,
                       uint32_t height, int fps, int bitrate,
                       const AudioConfig &audio) {
  (void)audio; // ponytail: audio stub; upgrade path IAudioClient -> MF sample
               // queue

  width_ = width;
  height_ = height;
  fps_ = fps;

  ComPtr<IMFAttributes> attrs;
  if (FAILED(MFCreateAttributes(&attrs, 1))) {
    return Status::fail("MFCreateAttributes failed");
  }
  attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

  ComPtr<IMFSinkWriter> writer;
  const HRESULT hrWriter =
      MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &writer);
  if (FAILED(hrWriter)) {
    return Status::fail("MFCreateSinkWriterFromURL failed: " +
                        formatHresult(hrWriter));
  }

  ComPtr<IMFMediaType> outType;
  if (FAILED(MFCreateMediaType(&outType))) {
    return Status::fail("MFCreateMediaType failed");
  }
  outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (auto st = setAttributeSize(outType.Get(), width, height); !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(outType.Get(), MF_MT_FRAME_RATE,
                                  static_cast<uint32_t>(fps), 1);
      !st.isOk()) {
    return st;
  }
  if (auto st =
          setAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      !st.isOk()) {
    return st;
  }
  outType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate));
  outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

  const HRESULT hrOut = writer->AddStream(outType.Get(), &streamIndex_);
  if (FAILED(hrOut)) {
    return Status::fail("AddStream(output) failed: " + formatHresult(hrOut));
  }

  ComPtr<IMFMediaType> inType;
  if (FAILED(MFCreateMediaType(&inType))) {
    return Status::fail("MFCreateMediaType failed");
  }
  inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (auto st = setAttributeSize(inType.Get(), width, height); !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(inType.Get(), MF_MT_FRAME_RATE,
                                  static_cast<uint32_t>(fps), 1);
      !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      !st.isOk()) {
    return st;
  }
  inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  inType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4));

  const HRESULT hrIn =
      writer->SetInputMediaType(streamIndex_, inType.Get(), nullptr);
  if (FAILED(hrIn)) {
    return Status::fail("SetInputMediaType failed: " + formatHresult(hrIn));
  }

  const HRESULT hrBegin = writer->BeginWriting();
  if (FAILED(hrBegin)) {
    return Status::fail("BeginWriting failed: " + formatHresult(hrBegin));
  }

  writer_ = writer;
  opened_ = true;
  return Status::ok();
}

Status MfEncoder::writeFrame(const std::vector<uint8_t> &bgra,
                             int64_t timestamp100ns) {
  if (!opened_ || !writer_) {
    return Status::fail("Encoder not opened");
  }

  // ponytail: MFCreateMemoryBuffer + MFCreateSample every frame; reuse needs
  // verified WriteSample copy semantics (may retain sample async) — defer pass
  // 2
  ComPtr<IMFMediaBuffer> buffer;
  const DWORD bufferSize = static_cast<DWORD>(width_ * height_ * 4);
  const HRESULT hrBuf = MFCreateMemoryBuffer(bufferSize, &buffer);
  if (FAILED(hrBuf)) {
    return Status::fail("MFCreateMemoryBuffer failed: " + formatHresult(hrBuf));
  }

  BYTE *data = nullptr;
  if (FAILED(buffer->Lock(&data, nullptr, nullptr))) {
    return Status::fail("Buffer lock failed");
  }
  std::memcpy(data, bgra.data(), bgra.size());
  buffer->Unlock();
  buffer->SetCurrentLength(bufferSize);

  ComPtr<IMFSample> sample;
  if (FAILED(MFCreateSample(&sample))) {
    return Status::fail("MFCreateSample failed");
  }
  sample->AddBuffer(buffer.Get());

  // Wall-clock timestamps keep playback speed correct when frames are dropped
  const LONGLONG duration = 10000000LL / fps_;
  sample->SetSampleTime(timestamp100ns);
  sample->SetSampleDuration(duration);

  const HRESULT hrWrite = writer_->WriteSample(streamIndex_, sample.Get());
  if (FAILED(hrWrite)) {
    return Status::fail("WriteSample failed: " + formatHresult(hrWrite));
  }
  return Status::ok();
}

Status MfEncoder::finalize() {
  if (!opened_ || !writer_) {
    return Status::ok();
  }
  const HRESULT hr = writer_->Finalize();
  writer_.Reset();
  opened_ = false;
  if (FAILED(hr)) {
    return Status::fail("Finalize failed: " + formatHresult(hr));
  }
  return Status::ok();
}
