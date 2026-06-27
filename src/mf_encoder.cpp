#include "mf_encoder.h"

#include "logging.h"

#include <mferror.h>

#include <cstring>

namespace {

template <typename T> class ComGuard {
public:
  ComGuard() = default;
  explicit ComGuard(T *ptr) : ptr_(ptr) {}
  ~ComGuard() {
    if (ptr_) {
      ptr_->Release();
    }
  }
  T *get() const { return ptr_; }
  T **put() { return &ptr_; }
  T *operator->() const { return ptr_; }

private:
  T *ptr_ = nullptr;
};

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

  ComGuard<IMFAttributes> attrs;
  if (FAILED(MFCreateAttributes(attrs.put(), 1))) {
    return Status::fail("MFCreateAttributes failed");
  }
  attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

  ComGuard<IMFSinkWriter> writer;
  const HRESULT hrWriter = MFCreateSinkWriterFromURL(path.c_str(), nullptr,
                                                     attrs.get(), writer.put());
  if (FAILED(hrWriter)) {
    return Status::fail("MFCreateSinkWriterFromURL failed: " +
                        formatHresult(hrWriter));
  }

  ComGuard<IMFMediaType> outType;
  if (FAILED(MFCreateMediaType(outType.put()))) {
    return Status::fail("MFCreateMediaType failed");
  }
  outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  if (auto st = setAttributeSize(outType.get(), width, height); !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(outType.get(), MF_MT_FRAME_RATE,
                                  static_cast<uint32_t>(fps), 1);
      !st.isOk()) {
    return st;
  }
  if (auto st =
          setAttributeRatio(outType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      !st.isOk()) {
    return st;
  }
  outType->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate));
  outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);

  const HRESULT hrOut = writer->AddStream(outType.get(), &streamIndex_);
  if (FAILED(hrOut)) {
    return Status::fail("AddStream(output) failed: " + formatHresult(hrOut));
  }

  ComGuard<IMFMediaType> inType;
  if (FAILED(MFCreateMediaType(inType.put()))) {
    return Status::fail("MFCreateMediaType failed");
  }
  inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  if (auto st = setAttributeSize(inType.get(), width, height); !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(inType.get(), MF_MT_FRAME_RATE,
                                  static_cast<uint32_t>(fps), 1);
      !st.isOk()) {
    return st;
  }
  if (auto st = setAttributeRatio(inType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
      !st.isOk()) {
    return st;
  }
  inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  inType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4));

  const HRESULT hrIn =
      writer->SetInputMediaType(streamIndex_, inType.get(), nullptr);
  if (FAILED(hrIn)) {
    return Status::fail("SetInputMediaType failed: " + formatHresult(hrIn));
  }

  const HRESULT hrBegin = writer->BeginWriting();
  if (FAILED(hrBegin)) {
    return Status::fail("BeginWriting failed: " + formatHresult(hrBegin));
  }

  writer_ = writer.get();
  writer.get()->AddRef();
  opened_ = true;
  return Status::ok();
}

Status MfEncoder::writeFrame(const std::vector<uint8_t> &bgra,
                             int64_t timestamp100ns) {
  if (!opened_ || !writer_) {
    return Status::fail("Encoder not opened");
  }

  ComGuard<IMFMediaBuffer> buffer;
  const DWORD bufferSize = static_cast<DWORD>(width_ * height_ * 4);
  const HRESULT hrBuf = MFCreateMemoryBuffer(bufferSize, buffer.put());
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

  ComGuard<IMFSample> sample;
  if (FAILED(MFCreateSample(sample.put()))) {
    return Status::fail("MFCreateSample failed");
  }
  sample->AddBuffer(buffer.get());

  // Wall-clock timestamps keep playback speed correct when frames are dropped
  const LONGLONG duration = 10000000LL / fps_;
  sample->SetSampleTime(timestamp100ns);
  sample->SetSampleDuration(duration);

  const HRESULT hrWrite = writer_->WriteSample(streamIndex_, sample.get());
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
  writer_->Release();
  writer_ = nullptr;
  opened_ = false;
  if (FAILED(hr)) {
    return Status::fail("Finalize failed: " + formatHresult(hr));
  }
  return Status::ok();
}
