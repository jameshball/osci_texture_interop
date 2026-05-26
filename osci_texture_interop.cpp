#include "osci_texture_interop.h"

#include "detail/osci_TextureInteropPrivate.h"

#include <utility>

namespace osci::texture {
namespace {

class UnavailableOpenGLSender final : public OpenGLSenderImpl {
public:
    ErrorCode start(OpenGLSenderDescription) override {
        return getPlatformOpenGLBackendStatus().error;
    }

    ErrorCode publish(OpenGLTextureFrame) override {
        return ErrorCode::invalidState;
    }

    void stop() override {
    }
};

class UnavailableOpenGLReceiver final : public OpenGLReceiverImpl {
public:
    std::vector<SourceInfo> listSources() override {
        return {};
    }

    ErrorCode connect(SourceInfo) override {
        return getPlatformOpenGLBackendStatus().error;
    }

    ErrorCode receive(ReceivedOpenGLTexture&) override {
        return ErrorCode::invalidState;
    }

    void disconnect() override {
    }
};

bool isValidDescription(const OpenGLSenderDescription& description) {
    if (description.sourceName.trim().isEmpty()) {
        return false;
    }
    if (description.width <= 0 || description.height <= 0) {
        return false;
    }

    return true;
}

bool isValidTexture(const OpenGLTextureFrame& frame) {
    if (frame.textureId == 0 || frame.textureTarget == 0) {
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    if (frame.originX != 0 || frame.originY != 0) {
        return false;
    }

    return true;
}

} // namespace

BackendStatus makeBackendStatus(bool available, ErrorCode error, juce::String message) {
    BackendStatus status;
    status.available = available;
    status.error = error;
    status.message = std::move(message);
    return status;
}

#if JUCE_MAC && !OSCI_TEXTURE_INTEROP_ENABLE_SYPHON
BackendStatus getPlatformOpenGLBackendStatus() {
    return makeBackendStatus(false,
                             ErrorCode::backendDisabled,
                             "Syphon OpenGL support is disabled in this build.");
}

std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender() {
    return std::make_unique<UnavailableOpenGLSender>();
}

std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver() {
    return std::make_unique<UnavailableOpenGLReceiver>();
}
#elif JUCE_WINDOWS && !OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
BackendStatus getPlatformOpenGLBackendStatus() {
    return makeBackendStatus(false,
                             ErrorCode::backendDisabled,
                             "Spout OpenGL support is disabled in this build.");
}

std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender() {
    return std::make_unique<UnavailableOpenGLSender>();
}

std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver() {
    return std::make_unique<UnavailableOpenGLReceiver>();
}
#elif !JUCE_MAC && !JUCE_WINDOWS
BackendStatus getPlatformOpenGLBackendStatus() {
    return makeBackendStatus(false,
                             ErrorCode::unsupportedPlatform,
                             "OpenGL texture sharing is only supported on macOS with Syphon and Windows with Spout.");
}

std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender() {
    return std::make_unique<UnavailableOpenGLSender>();
}

std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver() {
    return std::make_unique<UnavailableOpenGLReceiver>();
}
#endif

OpenGLSender::OpenGLSender() : impl(createPlatformOpenGLSender()) {}
OpenGLSender::~OpenGLSender() = default;

ErrorCode OpenGLSender::start(OpenGLSenderDescription description) {
    if (!isValidDescription(description)) {
        return ErrorCode::invalidState;
    }

    const ErrorCode error = impl->start(std::move(description));
    running.store(error == ErrorCode::none);
    return error;
}

ErrorCode OpenGLSender::publish(OpenGLTextureFrame frame) {
    if (!isValidTexture(frame)) {
        return ErrorCode::invalidTexture;
    }

    return impl->publish(frame);
}

void OpenGLSender::stop() {
    impl->stop();
    running.store(false);
}

bool OpenGLSender::isRunning() const {
    return running.load();
}

OpenGLReceiver::OpenGLReceiver() : impl(createPlatformOpenGLReceiver()) {}
OpenGLReceiver::~OpenGLReceiver() = default;

ErrorCode OpenGLReceiver::connect(SourceInfo source) {
    const ErrorCode error = impl->connect(std::move(source));
    connected.store(error == ErrorCode::none);
    return error;
}

ErrorCode OpenGLReceiver::receive(ReceivedOpenGLTexture& frame) {
    return impl->receive(frame);
}

void OpenGLReceiver::disconnect() {
    impl->disconnect();
    connected.store(false);
}

bool OpenGLReceiver::isConnected() const {
    return connected.load();
}

BackendStatus getOpenGLBackendStatus() {
    return getPlatformOpenGLBackendStatus();
}

std::vector<SourceInfo> listOpenGLSources() {
    std::unique_ptr<OpenGLReceiverImpl> receiver = createPlatformOpenGLReceiver();
    return receiver->listSources();
}

juce::String toString(ErrorCode error) {
    switch (error) {
        case ErrorCode::none: return "No error";
        case ErrorCode::unsupportedPlatform: return "Unsupported platform";
        case ErrorCode::backendDisabled: return "Backend disabled";
        case ErrorCode::sdkUnavailable: return "SDK unavailable";
        case ErrorCode::invalidState: return "Invalid state";
        case ErrorCode::invalidSource: return "Invalid source";
        case ErrorCode::invalidTexture: return "Invalid texture";
        case ErrorCode::graphicsContextMismatch: return "Graphics context mismatch";
        case ErrorCode::unsupportedPixelFormat: return "Unsupported pixel format";
        case ErrorCode::sourceNotFound: return "Source not found";
        case ErrorCode::connectionLost: return "Connection lost";
        case ErrorCode::frameTimeout: return "Frame timeout";
        case ErrorCode::publishFailed: return "Publish failed";
        case ErrorCode::receiveFailed: return "Receive failed";
    }

    return "Unknown";
}

} // namespace osci::texture

#if JUCE_WINDOWS && OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
#include "platform/osci_SpoutOpenGL.cpp"
#endif
