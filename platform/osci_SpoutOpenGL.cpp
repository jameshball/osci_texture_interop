#include "../detail/osci_TextureInteropPrivate.h"

#if JUCE_WINDOWS

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <windows.h>
#include <wincrypt.h>
#include <gl/GL.h>

#include "../third_party/spout2/2.007.017/include/SpoutLibrary/SpoutLibrary.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif

namespace osci::texture {
namespace {

using namespace juce::gl;

using GetSpoutFunction = SPOUTHANDLE (WINAPI*)();

constexpr std::array<unsigned char, 32> expectedSpoutDllSha256 {
    0x31, 0xf1, 0x22, 0x68, 0x16, 0x93, 0x97, 0xba,
    0xcd, 0x90, 0x48, 0x90, 0x3d, 0x9a, 0xd7, 0x11,
    0xfa, 0xe5, 0x8b, 0x78, 0x8e, 0x90, 0xa8, 0x60,
    0x59, 0x8b, 0x21, 0xb1, 0xda, 0xae, 0x62, 0xeb,
};

HMODULE& spoutLibraryHandle() {
    static HMODULE handle = nullptr;
    return handle;
}

juce::String spoutDllName() {
    return "SpoutLibrary.dll";
}

bool hasPinnedSpoutDllHash(const juce::File& file) {
    if (!file.existsAsFile()) {
        return false;
    }

    HANDLE fileHandle = CreateFileW(file.getFullPathName().toWideCharPointer(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool matches = false;

    if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
        && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        std::array<unsigned char, 8192> buffer {};
        DWORD bytesRead = 0;
        bool readOk = true;
        for (;;) {
            if (!ReadFile(fileHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                readOk = false;
                break;
            }
            if (bytesRead == 0) {
                break;
            }
            if (!CryptHashData(hash, buffer.data(), bytesRead, 0)) {
                readOk = false;
                break;
            }
        }

        if (readOk) {
            std::array<unsigned char, 32> digest {};
            DWORD digestSize = static_cast<DWORD>(digest.size());
            if (CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0) && digestSize == static_cast<DWORD>(digest.size())) {
                matches = digest == expectedSpoutDllSha256;
            }
        }
    }

    if (hash != 0) {
        CryptDestroyHash(hash);
    }
    if (provider != 0) {
        CryptReleaseContext(provider, 0);
    }

    CloseHandle(fileHandle);
    return matches;
}

juce::File commonSpoutLibraryFile() {
    return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
        .getChildFile("osci-render")
        .getChildFile(spoutDllName());
}

juce::File findSpoutLibraryFile() {
    const juce::File candidate = commonSpoutLibraryFile();
    if (hasPinnedSpoutDllHash(candidate)) {
        return candidate;
    }

    return {};
}

class SpoutInstance {
public:
    SpoutInstance() = default;

    ~SpoutInstance() {
        detach();
    }

    SpoutInstance(SpoutInstance&&) = delete;
    SpoutInstance& operator=(SpoutInstance&&) = delete;

    SpoutInstance(const SpoutInstance&) = delete;
    SpoutInstance& operator=(const SpoutInstance&) = delete;

    ErrorCode open() {
#if OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
        if (object != nullptr) {
            return ErrorCode::none;
        }

        const ErrorCode runtimeError = ensureRuntime();
        if (runtimeError != ErrorCode::none) {
            return runtimeError;
        }

        object = getSpout();
        if (object == nullptr) {
            return ErrorCode::sdkUnavailable;
        }

        return ErrorCode::none;
#else
        return ErrorCode::backendDisabled;
#endif
    }

    ErrorCode validateExports() {
#if OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
        detach();
        const ErrorCode runtimeError = ensureRuntime();
        if (runtimeError != ErrorCode::none) {
            return runtimeError;
        }

        return getSpout != nullptr ? ErrorCode::none : ErrorCode::sdkUnavailable;
#else
        return ErrorCode::backendDisabled;
#endif
    }

    void releaseSenderResources() {
        if (object != nullptr) {
            object->ReleaseSender();
        }
    }

    void detach() {
        if (object != nullptr) {
            object->Release();
        }

        object = nullptr;
        library = nullptr;
        getSpout = nullptr;
    }

    void releaseReceiverResources() {
        if (object != nullptr) {
            object->ReleaseReceiver();
        }
    }

    [[nodiscard]] SPOUTHANDLE get() const {
        return object;
    }

    GetSpoutFunction getSpout = nullptr;

private:
    bool resolveExports() {
        getSpout = reinterpret_cast<GetSpoutFunction>(GetProcAddress(library, "GetSpout"));
        return getSpout != nullptr;
    }

    ErrorCode ensureRuntime() {
        if (library != nullptr) {
            return getSpout != nullptr ? ErrorCode::none : ErrorCode::sdkUnavailable;
        }

        HMODULE& processLibrary = spoutLibraryHandle();
        if (processLibrary == nullptr) {
            const juce::File spoutLibrary = findSpoutLibraryFile();
            if (!spoutLibrary.existsAsFile()) {
                return ErrorCode::sdkUnavailable;
            }

            processLibrary = LoadLibraryExW(spoutLibrary.getFullPathName().toWideCharPointer(),
                                            nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }

        library = processLibrary;
        if (library == nullptr) {
            return ErrorCode::sdkUnavailable;
        }

        if (!resolveExports()) {
            getSpout = nullptr;
            library = nullptr;
            return ErrorCode::sdkUnavailable;
        }

        return ErrorCode::none;
    }

    HMODULE library = nullptr;
    SPOUTHANDLE object = nullptr;
};

BackendStatus spoutStatus() {
#if OSCI_TEXTURE_INTEROP_ENABLE_SPOUT
    if (!findSpoutLibraryFile().existsAsFile()) {
        return makeBackendStatus(false,
                                 ErrorCode::sdkUnavailable,
                                 "Pinned Spout 2.007.017 " + spoutDllName() + " was not found in the osci-render common application data folder.");
    }

    SpoutInstance probe;
    if (probe.validateExports() == ErrorCode::none) {
        return makeBackendStatus(true,
                                 ErrorCode::none,
                                 "Spout OpenGL texture sharing is available.");
    }

    return makeBackendStatus(false,
                             ErrorCode::sdkUnavailable,
                             spoutDllName() + " is not the pinned Spout 2.007.017 runtime expected by this build.");
#else
    return makeBackendStatus(false,
                             ErrorCode::backendDisabled,
                             "Spout OpenGL support is disabled in this build.");
#endif
}

juce::SpinLock& discoveryLock() {
    static juce::SpinLock lock;
    return lock;
}

SpoutInstance& discoveryInstance() {
    static SpoutInstance instance;
    return instance;
}

juce::String stringFromUtf8(const char* text) {
    if (text == nullptr) {
        return {};
    }

    return juce::String::fromUTF8(text);
}

std::string toUtf8(juce::String value) {
    return value.toStdString();
}

juce::String sourceNameFrom(SourceInfo source) {
    if (source.opaqueId.isNotEmpty()) {
        return source.opaqueId;
    }

    return source.displayName;
}

bool fillSenderInfo(SpoutInstance& spout, const juce::String& senderName, SourceInfo& source) {
    unsigned int width = 0;
    unsigned int height = 0;
    HANDLE shareHandle = nullptr;
    DWORD format = 0;
    const std::string name = toUtf8(senderName);
    const bool found = spout.get()->GetSenderInfo(name.c_str(), width, height, shareHandle, format);

    source.displayName = senderName;
    source.opaqueId = senderName;
    source.width = static_cast<int>(width);
    source.height = static_cast<int>(height);
    source.connectable = found;
    return source.connectable;
}

class ScopedTexture2DBinding {
public:
    ScopedTexture2DBinding() {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    }

    ~ScopedTexture2DBinding() {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    }

    ScopedTexture2DBinding(const ScopedTexture2DBinding&) = delete;
    ScopedTexture2DBinding& operator=(const ScopedTexture2DBinding&) = delete;

private:
    GLint previousTexture = 0;
};

void configureTextureStorage(GLuint textureId, int width, int height) {
    ScopedTexture2DBinding textureBinding;
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

class SpoutOpenGLSender final : public OpenGLSenderImpl {
public:
    ~SpoutOpenGLSender() override {
        stop();
    }

    ErrorCode start(OpenGLSenderDescription description) override {
        stop();

        const ErrorCode openError = spout.open();
        if (openError != ErrorCode::none) {
            return openError;
        }

        const std::string senderName = toUtf8(description.sourceName);
        spout.get()->SetSenderName(senderName.c_str());
        if (description.width > 0 && description.height > 0) {
            spout.get()->CreateSender(senderName.c_str(),
                                      static_cast<unsigned int>(description.width),
                                      static_cast<unsigned int>(description.height),
                                      0);
        }

        return ErrorCode::none;
    }

    ErrorCode publish(OpenGLTextureFrame frame) override {
        if (spout.get() == nullptr) {
            return ErrorCode::invalidState;
        }

        GLint boundFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &boundFramebuffer);

        bool sent = false;

        // Spout's GL/DX interop can otherwise observe the frame before the
        // renderer's queued OpenGL work is visible to the sharing path.
        glFinish();

        if (frame.textureId != 0 && frame.width > 0 && frame.height > 0) {
            ScopedTexture2DBinding textureBinding;
            sent = spout.get()->SendTexture(static_cast<GLuint>(frame.textureId),
                                            static_cast<GLuint>(frame.textureTarget),
                                            static_cast<unsigned int>(frame.width),
                                            static_cast<unsigned int>(frame.height),
                                            frame.verticallyFlipped,
                                            0);
        }

        if (!sent && boundFramebuffer > 0) {
            sent = spout.get()->SendFbo(static_cast<GLuint>(boundFramebuffer),
                                        static_cast<unsigned int>(frame.width),
                                        static_cast<unsigned int>(frame.height),
                                        frame.verticallyFlipped);
        }

        if (!sent) {
            return ErrorCode::publishFailed;
        }

        return ErrorCode::none;
    }

    void stop() override {
        if (spout.get() != nullptr) {
            spout.releaseSenderResources();
            spout.detach();
        }
    }

private:
    SpoutInstance spout;
};

class SpoutOpenGLReceiver final : public OpenGLReceiverImpl {
public:
    ~SpoutOpenGLReceiver() override {
        disconnect();
    }

    std::vector<SourceInfo> listSources() override {
        std::vector<SourceInfo> sources;

        juce::SpinLock::ScopedLockType discoveryScope(discoveryLock());
        SpoutInstance& discovery = discoveryInstance();
        const ErrorCode openError = discovery.open();
        if (openError != ErrorCode::none) {
            return sources;
        }

        const int count = discovery.get()->GetSenderCount();
        sources.reserve(static_cast<std::size_t>(std::max(count, 0)));

        for (int index = 0; index < count; ++index) {
            std::array<char, 256> name {};
            if (!discovery.get()->GetSender(index, name.data(), static_cast<int>(name.size()))) {
                continue;
            }

            SourceInfo source;
            if (fillSenderInfo(discovery, stringFromUtf8(name.data()), source)) {
                sources.push_back(std::move(source));
            }
        }

        return sources;
    }

    ErrorCode connect(SourceInfo source) override {
        disconnect();

        const juce::String senderName = sourceNameFrom(source);
        if (senderName.trim().isEmpty()) {
            return ErrorCode::invalidSource;
        }

        const ErrorCode openError = spout.open();
        if (openError != ErrorCode::none) {
            return openError;
        }

        const std::string name = toUtf8(senderName);
        if (!updateSenderDetails(name)) {
            disconnect();
            return ErrorCode::sourceNotFound;
        }

        connectedSource = source;
        connectedSource.displayName = connectedSource.displayName.isNotEmpty() ? connectedSource.displayName : senderName;
        connectedSource.opaqueId = senderName;
        connectedSource.connectable = true;
        spout.get()->SetReceiverName(name.c_str());
        return ensureTexture();
    }

    ErrorCode receive(ReceivedOpenGLTexture& frame) override {
        if (spout.get() == nullptr || connectedSource.opaqueId.isEmpty()) {
            return ErrorCode::invalidState;
        }

        const std::string name = toUtf8(connectedSource.opaqueId);
        if (!updateSenderDetails(name)) {
            return ErrorCode::connectionLost;
        }

        ErrorCode textureError = ensureTexture();
        if (textureError != ErrorCode::none) {
            return textureError;
        }

        ScopedTexture2DBinding textureBinding;
        bool received = spout.get()->ReceiveTexture(receiverTexture,
                                                    GL_TEXTURE_2D,
                                                    false,
                                                    0);
        if (consumeUpdateFlag()) {
            if (!updateSenderDetails(name)) {
                return ErrorCode::connectionLost;
            }
            textureError = ensureTexture();
            if (textureError != ErrorCode::none) {
                return textureError;
            }

            if (received) {
                return ErrorCode::receiveFailed;
            }

            received = spout.get()->ReceiveTexture(receiverTexture,
                                                   GL_TEXTURE_2D,
                                                   false,
                                                   0);
            if (consumeUpdateFlag()) {
                return ErrorCode::receiveFailed;
            }
        }

        if (!received) {
            return ErrorCode::receiveFailed;
        }

        // Ensure the received interop copy has completed before product code
        // reads the destination texture back into the image parser.
        glFinish();

        frame.source = connectedSource;
        frame.texture.textureId = receiverTexture;
        frame.texture.textureTarget = openGLTexture2D;
        frame.texture.width = connectedSource.width;
        frame.texture.height = connectedSource.height;
        frame.texture.verticallyFlipped = false;
        frame.texture.frameIndex = static_cast<std::uint64_t>(std::max<long>(spout.get()->GetSenderFrame(), 0));
        frame.newFrame = spout.get()->IsFrameNew();

        return ErrorCode::none;
    }

    void disconnect() override {
        releaseTexture();

        if (spout.get() != nullptr) {
            spout.releaseReceiverResources();
            spout.detach();
        }

        connectedSource = {};
    }

private:
    bool updateSenderDetails(const std::string& name) {
        if (spout.get() == nullptr || name.empty()) {
            return false;
        }

        unsigned int width = 0;
        unsigned int height = 0;
        HANDLE shareHandle = nullptr;
        DWORD format = 0;
        if (spout.get()->GetSenderInfo(name.c_str(), width, height, shareHandle, format)) {
            connectedSource.width = static_cast<int>(width);
            connectedSource.height = static_cast<int>(height);
            return connectedSource.width > 0 && connectedSource.height > 0;
        }

        return false;
    }

    bool consumeUpdateFlag() {
        return spout.get() != nullptr && spout.get()->IsUpdated();
    }

    ErrorCode ensureTexture() {
        if (connectedSource.width <= 0 || connectedSource.height <= 0) {
            return ErrorCode::invalidSource;
        }

        if (receiverTexture == 0) {
            glGenTextures(1, &receiverTexture);
        }

        if (receiverTexture == 0) {
            return ErrorCode::invalidTexture;
        }

        if (textureWidth != connectedSource.width || textureHeight != connectedSource.height) {
            textureWidth = connectedSource.width;
            textureHeight = connectedSource.height;
            configureTextureStorage(receiverTexture, textureWidth, textureHeight);
        }

        return ErrorCode::none;
    }

    void releaseTexture() {
        if (receiverTexture != 0) {
            if (wglGetCurrentContext() == nullptr) {
                jassertfalse;
                return;
            }

            glDeleteTextures(1, &receiverTexture);
            receiverTexture = 0;
        }

        textureWidth = 0;
        textureHeight = 0;
    }

    SpoutInstance spout;
    SourceInfo connectedSource;
    GLuint receiverTexture = 0;
    int textureWidth = 0;
    int textureHeight = 0;
};

} // namespace

BackendStatus getPlatformOpenGLBackendStatus() {
    return spoutStatus();
}

std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender() {
    return std::make_unique<SpoutOpenGLSender>();
}

std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver() {
    return std::make_unique<SpoutOpenGLReceiver>();
}

} // namespace osci::texture

#endif
