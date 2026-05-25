#pragma once

/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                osci_texture_interop
  vendor:            jameshball
  version:           1.0.0
  name:              osci texture interop
  description:       Product-level local texture interoperability boundary
  website:           https://osci-render.com
  license:           MIT
  minimumCppStandard: 20

  dependencies:      juce_core

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

#ifndef OSCI_TEXTURE_INTEROP_ENABLE_SYPHON
 #if JUCE_MAC
  #define OSCI_TEXTURE_INTEROP_ENABLE_SYPHON 1
 #else
  #define OSCI_TEXTURE_INTEROP_ENABLE_SYPHON 0
 #endif
#endif

#ifndef OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
 #if JUCE_WINDOWS
  #define OSCI_TEXTURE_INTEROP_ENABLE_SPOUT 1
 #else
  #define OSCI_TEXTURE_INTEROP_ENABLE_SPOUT 0
 #endif
#endif

namespace osci::texture {

inline constexpr std::uint32_t openGLTexture2D = 0x0DE1;
inline constexpr std::uint32_t openGLTextureRectangle = 0x84F5;

enum class ErrorCode {
    none,
    unsupportedPlatform,
    backendDisabled,
    sdkUnavailable,
    invalidState,
    invalidSource,
    invalidTexture,
    graphicsContextMismatch,
    unsupportedPixelFormat,
    sourceNotFound,
    connectionLost,
    frameTimeout,
    publishFailed,
    receiveFailed,
};

struct BackendStatus {
    bool available = false;
    ErrorCode error = ErrorCode::unsupportedPlatform;
    juce::String message;

    [[nodiscard]] bool isAvailable() const {
        return available && error == ErrorCode::none;
    }
};

struct SourceInfo {
    juce::String displayName;
    juce::String applicationName;
    juce::String opaqueId;
    int width = 0;
    int height = 0;
    bool connectable = false;
};

struct OpenGLSenderDescription {
    juce::String sourceName;
    int width = 0;
    int height = 0;
};

struct OpenGLTextureFrame {
    std::uint32_t textureId = 0;
    std::uint32_t textureTarget = openGLTexture2D;
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    bool verticallyFlipped = false;
    std::uint64_t frameIndex = 0;
};

struct ReceivedOpenGLTexture {
    SourceInfo source;
    OpenGLTextureFrame texture;
    bool newFrame = false;
};

class OpenGLSenderImpl;
class OpenGLReceiverImpl;

class OpenGLSender {
public:
    OpenGLSender();
    ~OpenGLSender();

    OpenGLSender(OpenGLSender&&) = delete;
    OpenGLSender& operator=(OpenGLSender&&) = delete;

    OpenGLSender(const OpenGLSender&) = delete;
    OpenGLSender& operator=(const OpenGLSender&) = delete;

    ErrorCode start(OpenGLSenderDescription description);
    ErrorCode publish(OpenGLTextureFrame frame);
    void stop();

private:
    std::unique_ptr<OpenGLSenderImpl> impl;
};

class OpenGLReceiver {
public:
    OpenGLReceiver();
    ~OpenGLReceiver();

    OpenGLReceiver(OpenGLReceiver&&) = delete;
    OpenGLReceiver& operator=(OpenGLReceiver&&) = delete;

    OpenGLReceiver(const OpenGLReceiver&) = delete;
    OpenGLReceiver& operator=(const OpenGLReceiver&) = delete;

    ErrorCode connect(SourceInfo source);
    ErrorCode receive(ReceivedOpenGLTexture& frame);
    void disconnect();

private:
    std::unique_ptr<OpenGLReceiverImpl> impl;
};

BackendStatus getOpenGLBackendStatus();
std::vector<SourceInfo> listOpenGLSources();
juce::String toString(ErrorCode error);

} // namespace osci::texture
