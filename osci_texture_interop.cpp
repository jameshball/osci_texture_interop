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

void OpenGLTexturePublisher::setSourceName(juce::String newSourceName) {
    juce::SpinLock::ScopedLockType lock(sourceNameLock);
    sourceName = std::move(newSourceName);
}

bool OpenGLTexturePublisher::isRunning() const {
    return sender.isRunning();
}

void OpenGLTexturePublisher::stop() {
    sender.stop();
    resetState();
}

ServiceResult OpenGLTexturePublisher::service(bool shouldRun, OpenGLTextureFrame frame) {
    if (!shouldRun) {
        if (!sender.isRunning()) {
            return {};
        }

        stop();
        return { ServiceEvent::stopped };
    }

    ServiceEvent successEvent = ServiceEvent::none;
    if (!sender.isRunning()) {
        const ErrorCode error = start(frame);
        if (error != ErrorCode::none) {
            return fail(error);
        }

        successEvent = ServiceEvent::started;
    } else if (frame.width != runningWidth || frame.height != runningHeight) {
        sender.stop();
        resetState();

        const ErrorCode error = start(frame);
        if (error != ErrorCode::none) {
            return fail(error);
        }

        successEvent = ServiceEvent::restarted;
    }

    frame.frameIndex = frameIndex++;
    const ErrorCode publishError = sender.publish(frame);
    if (publishError != ErrorCode::none) {
        return fail(publishError, toString(publishError));
    }

    return { successEvent };
}

ServiceResult OpenGLTexturePublisher::serviceTexture2D(bool shouldRun, std::uint32_t textureId, int width, int height) {
    OpenGLTextureFrame frame;
    frame.textureId = textureId;
    frame.textureTarget = openGLTexture2D;
    frame.width = width;
    frame.height = height;
    return service(shouldRun, frame);
}

ErrorCode OpenGLTexturePublisher::start(OpenGLTextureFrame frame) {
    OpenGLSenderDescription description;
    description.sourceName = getSourceName();
    description.width = frame.width;
    description.height = frame.height;

    const ErrorCode error = sender.start(std::move(description));
    if (error == ErrorCode::none) {
        frameIndex = 0;
        runningWidth = frame.width;
        runningHeight = frame.height;
    }

    return error;
}

ServiceResult OpenGLTexturePublisher::fail(ErrorCode error, juce::String message) {
    sender.stop();
    resetState();

    if (message.isEmpty()) {
        const BackendStatus status = getOpenGLBackendStatus();
        message = status.isAvailable() || status.message.isEmpty()
            ? toString(error)
            : status.message;
    }

    return { ServiceEvent::failed, error, std::move(message) };
}

void OpenGLTexturePublisher::resetState() {
    frameIndex = 0;
    runningWidth = 0;
    runningHeight = 0;
}

juce::String OpenGLTexturePublisher::getSourceName() const {
    juce::SpinLock::ScopedLockType lock(sourceNameLock);
    return sourceName;
}

class InvisibleOpenGLContextComponent final : public juce::Component {
public:
    explicit InvisibleOpenGLContextComponent(juce::OpenGLRenderer* renderer) {
        setSize(1, 1);
        setBounds(0, 0, 1, 1);
        setVisible(true);
        setOpaque(false);
        context.setComponentPaintingEnabled(false);
        context.setRenderer(renderer);
        context.setContinuousRepainting(true);
        context.attachTo(*this);
        addToDesktop(juce::ComponentPeer::windowIsTemporary
                     | juce::ComponentPeer::windowIgnoresKeyPresses
                     | juce::ComponentPeer::windowIgnoresMouseClicks);
    }

    ~InvisibleOpenGLContextComponent() override {
        context.detach();
    }

private:
    juce::OpenGLContext context;
};

OpenGLTextureFrameGrabber::OpenGLTextureFrameGrabber(SourceInfo source) : source(std::move(source)) {
    openGLComponent = std::make_unique<InvisibleOpenGLContextComponent>(static_cast<juce::OpenGLRenderer*>(this));
}

OpenGLTextureFrameGrabber::~OpenGLTextureFrameGrabber() {
    wanted.store(false);
    openGLComponent = nullptr;
    receiver.disconnect();
}

void OpenGLTextureFrameGrabber::stop() {
    wanted.store(false);
}

bool OpenGLTextureFrameGrabber::isActive() const {
    return wanted.load() || receiver.isConnected() || processorStarted.load();
}

juce::String OpenGLTextureFrameGrabber::getSourceName() const {
    return source.displayName.isNotEmpty() ? source.displayName : "Texture Input";
}

void OpenGLTextureFrameGrabber::newOpenGLContextCreated() {
    wanted.store(true);
}

void OpenGLTextureFrameGrabber::renderOpenGL() {
    serviceFrame();
}

void OpenGLTextureFrameGrabber::openGLContextClosing() {
    disconnect(true);
    if (readbackFbo != 0) {
        GLuint fbo = static_cast<GLuint>(readbackFbo);
        juce::gl::glDeleteFramebuffers(1, &fbo);
        readbackFbo = 0;
    }
    readbackPixels.clear();
}

void OpenGLTextureFrameGrabber::notifyStartedAsync(SourceInfo receivedSource, std::vector<std::uint8_t> initialFrame, int width, int height, bool verticallyFlipped) {
    if (inputStarted == nullptr) {
        return;
    }

    if (startNotified.exchange(true)) {
        return;
    }

    juce::Component::SafePointer<OpenGLTextureFrameGrabber> safeThis(this);
    juce::MessageManager::callAsync([safeThis, receivedSource, initialFrame = std::move(initialFrame), width, height, verticallyFlipped] {
        if (safeThis == nullptr || safeThis->inputStarted == nullptr) {
            return;
        }

        const juce::String name = receivedSource.displayName.isNotEmpty() ? receivedSource.displayName : "Texture Input";
        safeThis->inputStarted(name, width, height);
        safeThis->processorStarted.store(true);
        if (!initialFrame.empty() && safeThis->frameReady != nullptr) {
            safeThis->frameReady(initialFrame, width, height, verticallyFlipped);
        }
    });
}

void OpenGLTextureFrameGrabber::notifyStoppedAsync() {
    if (!startNotified.exchange(false)) {
        return;
    }

    juce::Component::SafePointer<OpenGLTextureFrameGrabber> safeThis(this);
    auto callback = inputStopped;
    processorStarted.store(false);
    juce::MessageManager::callAsync([safeThis, callback] {
        if (safeThis != nullptr && callback) {
            callback();
        }
    });
}

void OpenGLTextureFrameGrabber::disconnect(bool notifyProcessor) {
    receiver.disconnect();
    lastFrameIndex = std::numeric_limits<std::uint64_t>::max();
    lastConnectError.store(ErrorCode::none);

    if (notifyProcessor) {
        notifyStoppedAsync();
    } else {
        startNotified.store(false);
        processorStarted.store(false);
    }
}

bool OpenGLTextureFrameGrabber::readFrame(const ReceivedOpenGLTexture& received, juce::String& failureMessage) {
    using namespace juce::gl;

    const auto& texture = received.texture;
    if (texture.textureId == 0 || texture.width <= 0 || texture.height <= 0) {
        return false;
    }

    const size_t width = static_cast<size_t>(texture.width);
    const size_t height = static_cast<size_t>(texture.height);
    if (width > maxReadbackDimension || height > maxReadbackDimension || height > maxReadbackBytes / 4 / width) {
        failureMessage = "The selected texture is too large to read safely.";
        return false;
    }

    if (readbackFbo == 0) {
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        readbackFbo = fbo;
    }

    if (readbackFbo == 0) {
        return false;
    }

    GLint previousPackAlignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(readbackFbo));
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           static_cast<GLenum>(texture.textureTarget),
                           static_cast<GLuint>(texture.textureId),
                           0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    bool success = framebufferStatus == GL_FRAMEBUFFER_COMPLETE;
    if (success) {
        readbackPixels.resize(width * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(texture.originX,
                     texture.originY,
                     texture.width,
                     texture.height,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     readbackPixels.data());
        const GLenum error = glGetError();
        success = error == GL_NO_ERROR;
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           static_cast<GLenum>(texture.textureTarget),
                           0,
                           0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    return success;
}

void OpenGLTextureFrameGrabber::fail(ErrorCode error, juce::String message) {
    wanted.store(false);

    if (message.isEmpty()) {
        const BackendStatus status = getOpenGLBackendStatus();
        message = status.isAvailable() || status.message.isEmpty()
            ? toString(error)
            : status.message;
    }

    auto callback = inputFailed;
    juce::MessageManager::callAsync([callback, message] {
        if (callback) {
            callback(message);
        }
    });

    disconnect(true);
}

void OpenGLTextureFrameGrabber::serviceFrame() {
    if (!wanted.load()) {
        if (receiver.isConnected() || startNotified.load() || processorStarted.load()) {
            disconnect(true);
        }
        return;
    }

    if (!receiver.isConnected()) {
        const ErrorCode error = receiver.connect(source);
        if (error != ErrorCode::none) {
            const ErrorCode previousError = lastConnectError.load();
            if (previousError != error) {
                lastConnectError.store(error);
            }
            if (error == ErrorCode::sourceNotFound || error == ErrorCode::connectionLost) {
                fail(error, missingSourceMessage());
                return;
            }

            fail(error);
            return;
        }

        lastConnectError.store(ErrorCode::none);
    }

    ReceivedOpenGLTexture received;
    const ErrorCode error = receiver.receive(received);
    if (error == ErrorCode::receiveFailed) {
        return;
    }

    if (error != ErrorCode::none) {
        if (error == ErrorCode::sourceNotFound || error == ErrorCode::connectionLost) {
            juce::String message = "The selected texture input source was removed.";
            const juce::String sourceName = getSourceName();
            if (sourceName.isNotEmpty() && sourceName != "Texture Input") {
                message = "The texture input source \"" + sourceName + "\" was removed.";
            }

            fail(error, message);
            return;
        }

        fail(error);
        return;
    }

    if (!received.newFrame && received.texture.frameIndex == lastFrameIndex) {
        return;
    }

    juce::String readFailureMessage;
    if (!readFrame(received, readFailureMessage)) {
        if (readFailureMessage.isEmpty()) {
            readFailureMessage = "The selected texture could not be read by this OpenGL context.";
        }

        fail(ErrorCode::receiveFailed, readFailureMessage);
        return;
    }

    lastFrameIndex = received.texture.frameIndex;
    if (!startNotified.load()) {
        SourceInfo receivedSource = received.source;
        receivedSource.width = received.texture.width;
        receivedSource.height = received.texture.height;
        notifyStartedAsync(receivedSource, readbackPixels, received.texture.width, received.texture.height, true);
        return;
    }

    if (!processorStarted.load()) {
        return;
    }

    if (frameReady != nullptr) {
        frameReady(readbackPixels, received.texture.width, received.texture.height, true);
    }
}

juce::String OpenGLTextureFrameGrabber::missingSourceMessage() const {
    const juce::String sourceName = getSourceName();
    if (sourceName.isNotEmpty() && sourceName != "Texture Input") {
        return "The texture input source \"" + sourceName + "\" is no longer available.";
    }

    return "The selected texture input source is no longer available.";
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
