#pragma once

#include "../osci_texture_interop.h"

namespace osci::texture {

class OpenGLSenderImpl {
public:
    virtual ~OpenGLSenderImpl() = default;

    virtual ErrorCode start(OpenGLSenderDescription description) = 0;
    virtual ErrorCode publish(OpenGLTextureFrame frame) = 0;
    virtual void stop() = 0;
};

class OpenGLReceiverImpl {
public:
    virtual ~OpenGLReceiverImpl() = default;

    virtual std::vector<SourceInfo> listSources() = 0;
    virtual ErrorCode connect(SourceInfo source) = 0;
    virtual ErrorCode receive(ReceivedOpenGLTexture& frame) = 0;
    virtual void disconnect() = 0;
};

BackendStatus makeBackendStatus(bool available, ErrorCode error, juce::String message);
BackendStatus getPlatformOpenGLBackendStatus();
std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender();
std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver();

} // namespace osci::texture
