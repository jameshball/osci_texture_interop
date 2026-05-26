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

  dependencies:      juce_core juce_gui_basics juce_opengl

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
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
class InvisibleOpenGLContextComponent;

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
    [[nodiscard]] bool isRunning() const;

private:
    std::unique_ptr<OpenGLSenderImpl> impl;
    std::atomic<bool> running = false;
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
    [[nodiscard]] bool isConnected() const;

private:
    std::unique_ptr<OpenGLReceiverImpl> impl;
    std::atomic<bool> connected = false;
};

enum class ServiceEvent {
    none,
    started,
    stopped,
    restarted,
    failed,
};

struct ServiceResult {
    ServiceEvent event = ServiceEvent::none;
    ErrorCode error = ErrorCode::none;
    juce::String message;

    [[nodiscard]] bool failed() const {
        return event == ServiceEvent::failed;
    }

    [[nodiscard]] bool changed() const {
        return event != ServiceEvent::none;
    }
};

class OpenGLTexturePublisher {
public:
    void setSourceName(juce::String sourceName);
    [[nodiscard]] bool isRunning() const;
    void stop();
    ServiceResult service(bool shouldRun, OpenGLTextureFrame frame);
    ServiceResult serviceTexture2D(bool shouldRun, std::uint32_t textureId, int width, int height);

private:
    ErrorCode start(OpenGLTextureFrame frame);
    ServiceResult fail(ErrorCode error, juce::String message = {});
    void resetState();
    [[nodiscard]] juce::String getSourceName() const;

    OpenGLSender sender;
    mutable juce::SpinLock sourceNameLock;
    juce::String sourceName;
    std::uint64_t frameIndex = 0;
    int runningWidth = 0;
    int runningHeight = 0;
};

class OpenGLTextureFrameGrabber final : public juce::Component, private juce::OpenGLRenderer {
public:
    explicit OpenGLTextureFrameGrabber(SourceInfo source);
    ~OpenGLTextureFrameGrabber() override;

    std::function<void(juce::String, int, int)> inputStarted;
    std::function<void(const std::vector<std::uint8_t>&, int, int, bool)> frameReady;
    std::function<void()> inputStopped;
    std::function<void(juce::String)> inputFailed;

    void stop();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] juce::String getSourceName() const;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void notifyStartedAsync(SourceInfo receivedSource, std::vector<std::uint8_t> initialFrame, int width, int height, bool verticallyFlipped);
    void notifyStoppedAsync();
    void disconnect(bool notifyProcessor);
    bool readFrame(const ReceivedOpenGLTexture& received, juce::String& failureMessage);
    void fail(ErrorCode error, juce::String message = {});
    void serviceFrame();
    [[nodiscard]] juce::String missingSourceMessage() const;

    static constexpr size_t maxReadbackDimension = 8192;
    static constexpr size_t maxReadbackBytes = 64 * 1024 * 1024;

    SourceInfo source;
    OpenGLReceiver receiver;
    std::unique_ptr<InvisibleOpenGLContextComponent> openGLComponent;
    std::atomic<bool> wanted = false;
    std::atomic<bool> startNotified = false;
    std::atomic<bool> processorStarted = false;
    std::atomic<ErrorCode> lastConnectError = ErrorCode::none;
    std::uint32_t readbackFbo = 0;
    std::vector<std::uint8_t> readbackPixels;
    std::uint64_t lastFrameIndex = std::numeric_limits<std::uint64_t>::max();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGLTextureFrameGrabber)
    JUCE_DECLARE_WEAK_REFERENCEABLE(OpenGLTextureFrameGrabber)
};

BackendStatus getOpenGLBackendStatus();
std::vector<SourceInfo> listOpenGLSources();
juce::String toString(ErrorCode error);

} // namespace osci::texture
