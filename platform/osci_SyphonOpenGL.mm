#include "../detail/osci_TextureInteropPrivate.h"

#if JUCE_MAC

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

@protocol OsciSyphonOpenGLServer
- (id)initWithName:(NSString*)serverName context:(CGLContextObj)context options:(NSDictionary*)options;
- (void)publishFrameTexture:(GLuint)texID textureTarget:(GLenum)target imageRegion:(NSRect)region textureDimensions:(NSSize)size flipped:(BOOL)isFlipped;
- (void)stop;
@end

@protocol OsciSyphonServerDirectory
@property (readonly) NSArray<NSDictionary<NSString*, id>*>* servers;
@end

@protocol OsciSyphonServerDirectoryFactory
+ (id<OsciSyphonServerDirectory>)sharedDirectory;
@end

@protocol OsciSyphonOpenGLClient
@property (readonly) BOOL isValid;
@property (readonly) BOOL hasNewFrame;
- (id)initWithServerDescription:(NSDictionary<NSString*, id>*)description context:(CGLContextObj)context options:(NSDictionary*)options newFrameHandler:(void (^)(id client))handler;
- (id)newFrameImage;
- (void)stop;
@end

@protocol OsciSyphonOpenGLImage
@property (readonly) GLuint textureName;
@property (readonly) NSSize textureSize;
@end

namespace osci::texture {
namespace {

NSString* toNSString(const juce::String& value) {
    return [NSString stringWithUTF8String:value.toRawUTF8()];
}

juce::String toJuceString(id value) {
    if ([value isKindOfClass:[NSString class]]) {
        return juce::String([(NSString*)value UTF8String]);
    }

    return {};
}

NSArray<NSString*>* syphonFrameworkPaths() {
    NSMutableArray<NSString*>* paths = [NSMutableArray array];

    NSString* privateFrameworksPath = [[NSBundle mainBundle] privateFrameworksPath];
    if (privateFrameworksPath != nil) {
        [paths addObject:[privateFrameworksPath stringByAppendingPathComponent:@"Syphon.framework"]];
    }

    [paths addObject:@"/Library/Frameworks/Syphon.framework"];
    [paths addObject:[@"~/Library/Frameworks/Syphon.framework" stringByExpandingTildeInPath]];
    return paths;
}

#if OSCI_TEXTURE_INTEROP_ENABLE_SYPHON
bool syphonFrameworkExists() {
    if (NSClassFromString(@"SyphonOpenGLServer") != Nil) {
        return true;
    }

    NSFileManager* fileManager = [NSFileManager defaultManager];
    for (NSString* path in syphonFrameworkPaths()) {
        if ([fileManager fileExistsAtPath:path]) {
            return true;
        }
    }

    return false;
}
#endif

bool loadSyphonFramework() {
    if (NSClassFromString(@"SyphonOpenGLServer") != Nil) {
        return true;
    }

    for (NSString* path in syphonFrameworkPaths()) {
        NSBundle* bundle = [NSBundle bundleWithPath:path];
        if (bundle == nil) {
            continue;
        }

        if ([bundle load]) {
            if (NSClassFromString(@"SyphonOpenGLServer") != Nil) {
                return true;
            }
        }
    }

    return false;
}

BackendStatus syphonStatus() {
#if OSCI_TEXTURE_INTEROP_ENABLE_SYPHON
    if (syphonFrameworkExists()) {
        return makeBackendStatus(true,
                                 ErrorCode::none,
                                 "Syphon OpenGL texture sharing is available.");
    }

    return makeBackendStatus(false,
                             ErrorCode::sdkUnavailable,
                             "Syphon.framework was not found.");
#else
    return makeBackendStatus(false,
                             ErrorCode::backendDisabled,
                             "Syphon OpenGL support is disabled in this build.");
#endif
}

NSString* syphonUUIDKey() {
    return @"SyphonServerDescriptionUUIDKey";
}

NSString* syphonNameKey() {
    return @"SyphonServerDescriptionNameKey";
}

NSString* syphonAppNameKey() {
    return @"SyphonServerDescriptionAppNameKey";
}

SourceInfo sourceInfoFromDescription(NSDictionary<NSString*, id>* description) {
    SourceInfo source;
    source.displayName = toJuceString([description objectForKey:syphonNameKey()]);
    source.applicationName = toJuceString([description objectForKey:syphonAppNameKey()]);
    source.opaqueId = toJuceString([description objectForKey:syphonUUIDKey()]);
    source.connectable = source.opaqueId.isNotEmpty() || source.displayName.isNotEmpty() || source.applicationName.isNotEmpty();

    if (source.displayName.isEmpty()) {
        source.displayName = source.applicationName.isNotEmpty() ? source.applicationName : "Syphon Source";
    }

    return source;
}

NSArray<NSDictionary<NSString*, id>*>* syphonServerDescriptions(bool refreshOnMainThread) {
    if (!loadSyphonFramework()) {
        return @[];
    }

    Class directoryClass = NSClassFromString(@"SyphonServerDirectory");
    if (directoryClass == Nil) {
        return @[];
    }

    id<OsciSyphonServerDirectory> directory = [(Class<OsciSyphonServerDirectoryFactory>)directoryClass sharedDirectory];
    if (directory == nil) {
        return @[];
    }

    if (refreshOnMainThread && [NSThread isMainThread]) {
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    return [directory servers];
}

NSDictionary<NSString*, id>* findDescriptionForSource(const SourceInfo& source) {
    NSArray<NSDictionary<NSString*, id>*>* descriptions = syphonServerDescriptions(false);
    for (NSDictionary<NSString*, id>* description in descriptions) {
        SourceInfo candidate = sourceInfoFromDescription(description);
        if (source.opaqueId.isNotEmpty() && candidate.opaqueId == source.opaqueId) {
            return description;
        }
    }

    for (NSDictionary<NSString*, id>* description in descriptions) {
        SourceInfo candidate = sourceInfoFromDescription(description);
        if (candidate.displayName == source.displayName && candidate.applicationName == source.applicationName) {
            return description;
        }
    }

    return nil;
}

NSRect syphonImageRegionForFrame(const OpenGLTextureFrame& frame) {
    return NSMakeRect(0.0, 0.0, static_cast<CGFloat>(frame.width), static_cast<CGFloat>(frame.height));
}

constexpr GLenum syphonTextureRectangle = static_cast<GLenum>(openGLTextureRectangle);
constexpr GLenum syphonTextureBindingRectangle = 0x84F6;

void clearOpenGLErrors() {
    while (glGetError() != GL_NO_ERROR) {}
}

class ScopedSyphonPublishState {
public:
    ScopedSyphonPublishState() {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
        glGetIntegerv(syphonTextureBindingRectangle, &previousTextureRectangle);
        previousScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        previousBlendEnabled = glIsEnabled(GL_BLEND);
        previousDepthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        previousCullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    }

    ~ScopedSyphonPublishState() {
        restore();
    }

    ScopedSyphonPublishState(const ScopedSyphonPublishState&) = delete;
    ScopedSyphonPublishState& operator=(const ScopedSyphonPublishState&) = delete;

    void prepareForPublish(int width, int height) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glDrawBuffer(GL_BACK);
        glViewport(0, 0, width, height);
        setEnabled(GL_SCISSOR_TEST, GL_FALSE);
        setEnabled(GL_BLEND, GL_FALSE);
        setEnabled(GL_DEPTH_TEST, GL_FALSE);
        setEnabled(GL_CULL_FACE, GL_FALSE);
        glUseProgram(0);
    }

    [[nodiscard]] GLint getPreviousProgram() const {
        return previousProgram;
    }

private:
    static void setEnabled(GLenum capability, GLboolean enabled) {
        if (enabled) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }

    void restore() {
        glUseProgram(static_cast<GLuint>(previousProgram));
        setEnabled(GL_CULL_FACE, previousCullFaceEnabled);
        setEnabled(GL_DEPTH_TEST, previousDepthTestEnabled);
        setEnabled(GL_BLEND, previousBlendEnabled);
        setEnabled(GL_SCISSOR_TEST, previousScissorEnabled);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glReadBuffer(static_cast<GLenum>(previousReadBuffer));
        glDrawBuffer(static_cast<GLenum>(previousDrawBuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(syphonTextureRectangle, static_cast<GLuint>(previousTextureRectangle));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture2D));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    }

    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousReadFramebuffer = 0;
    GLint previousDrawFramebuffer = 0;
    GLint previousReadBuffer = GL_BACK;
    GLint previousDrawBuffer = GL_BACK;
    GLint previousProgram = 0;
    GLint previousViewport[4] = {};
    GLint previousScissorBox[4] = {};
    GLint previousTexture2D = 0;
    GLint previousTextureRectangle = 0;
    GLboolean previousScissorEnabled = GL_FALSE;
    GLboolean previousBlendEnabled = GL_FALSE;
    GLboolean previousDepthTestEnabled = GL_FALSE;
    GLboolean previousCullFaceEnabled = GL_FALSE;
};

class SyphonPublishTexture {
public:
    ~SyphonPublishTexture() {
        release();
    }

    SyphonPublishTexture(const SyphonPublishTexture&) = delete;
    SyphonPublishTexture& operator=(const SyphonPublishTexture&) = delete;

    SyphonPublishTexture() = default;

    bool updateFrom(const OpenGLTextureFrame& source) {
        valid = false;
        textureId = 0;
        textureTarget = syphonTextureRectangle;

        if (source.textureId == 0 || source.width <= 0 || source.height <= 0) {
            return false;
        }

        if (source.textureTarget == openGLTextureRectangle) {
            textureId = static_cast<GLuint>(source.textureId);
            textureTarget = static_cast<GLenum>(source.textureTarget);
            valid = true;
            return true;
        }

        if (source.textureTarget != openGLTexture2D || !ensureCopyTarget(source.width, source.height)) {
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, copyFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               static_cast<GLenum>(source.textureTarget),
                               static_cast<GLuint>(source.textureId),
                               0);

        clearOpenGLErrors();
        bool copied = false;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindTexture(syphonTextureRectangle, copyTexture);
            clearOpenGLErrors();
            glCopyTexSubImage2D(syphonTextureRectangle, 0, 0, 0, 0, 0, source.width, source.height);
            copied = glGetError() == GL_NO_ERROR;
        }

        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               static_cast<GLenum>(source.textureTarget),
                               0,
                               0);

        if (!copied) {
            return false;
        }

        textureId = copyTexture;
        textureTarget = syphonTextureRectangle;
        valid = true;
        return true;
    }

    void release() {
        if (copyTexture == 0 && copyFramebuffer == 0) {
            textureId = 0;
            valid = false;
            return;
        }

        if (CGLGetCurrentContext() == nullptr) {
            jassertfalse;
            return;
        }

        if (copyTexture != 0) {
            glDeleteTextures(1, &copyTexture);
        }
        if (copyFramebuffer != 0) {
            glDeleteFramebuffers(1, &copyFramebuffer);
        }
        copyTexture = 0;
        copyFramebuffer = 0;
        copyTextureWidth = 0;
        copyTextureHeight = 0;
        textureId = 0;
        valid = false;
    }

    [[nodiscard]] bool isValid() const {
        return valid;
    }

    [[nodiscard]] GLuint getTextureId() const {
        return textureId;
    }

    [[nodiscard]] GLenum getTextureTarget() const {
        return textureTarget;
    }

private:
    bool ensureCopyTarget(int width, int height) {
        if (copyFramebuffer == 0) {
            glGenFramebuffers(1, &copyFramebuffer);
        }

        if (copyTexture == 0) {
            glGenTextures(1, &copyTexture);
            copyTextureWidth = 0;
            copyTextureHeight = 0;
        }

        if (copyFramebuffer == 0 || copyTexture == 0) {
            return false;
        }

        glBindTexture(syphonTextureRectangle, copyTexture);
        if (copyTextureWidth == width && copyTextureHeight == height) {
            return true;
        }

        glTexParameteri(syphonTextureRectangle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(syphonTextureRectangle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(syphonTextureRectangle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(syphonTextureRectangle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        clearOpenGLErrors();
        glTexImage2D(syphonTextureRectangle, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (glGetError() != GL_NO_ERROR) {
            copyTextureWidth = 0;
            copyTextureHeight = 0;
            return false;
        }

        copyTextureWidth = width;
        copyTextureHeight = height;
        return true;
    }

    GLuint copyFramebuffer = 0;
    GLuint copyTexture = 0;
    int copyTextureWidth = 0;
    int copyTextureHeight = 0;
    GLuint textureId = 0;
    GLenum textureTarget = syphonTextureRectangle;
    bool valid = false;
};

class SyphonOpenGLSender final : public OpenGLSenderImpl {
public:
    ~SyphonOpenGLSender() override {
        stop();
    }

    ErrorCode start(OpenGLSenderDescription description) override {
        @autoreleasepool {
            stop();

            if (!loadSyphonFramework()) {
                return ErrorCode::sdkUnavailable;
            }

            CGLContextObj context = CGLGetCurrentContext();
            if (context == nullptr) {
                return ErrorCode::graphicsContextMismatch;
            }

            Class serverClass = NSClassFromString(@"SyphonOpenGLServer");
            if (serverClass == Nil) {
                return ErrorCode::sdkUnavailable;
            }

            NSString* sourceName = toNSString(description.sourceName);
            id<OsciSyphonOpenGLServer> allocatedServer = (id<OsciSyphonOpenGLServer>)[serverClass alloc];
            server = [allocatedServer initWithName:sourceName context:context options:nil];
            if (server == nil) {
                return ErrorCode::invalidState;
            }

            return ErrorCode::none;
        }
    }

    ErrorCode publish(OpenGLTextureFrame frame) override {
        @autoreleasepool {
            if (server == nil) {
                return ErrorCode::invalidState;
            }

            ScopedSyphonPublishState publishState;
            if (!publishTexture.updateFrom(frame)) {
                return ErrorCode::publishFailed;
            }

            glBindTexture(publishTexture.getTextureTarget(), publishTexture.getTextureId());

            NSRect region = syphonImageRegionForFrame(frame);
            NSSize size = NSMakeSize(static_cast<CGFloat>(frame.width), static_cast<CGFloat>(frame.height));

            publishState.prepareForPublish(frame.width, frame.height);
            [server publishFrameTexture:publishTexture.getTextureId()
                           textureTarget:publishTexture.getTextureTarget()
                             imageRegion:region
                       textureDimensions:size
                                  flipped:frame.verticallyFlipped ? YES : NO];

            return ErrorCode::none;
        }
    }

    void stop() override {
        @autoreleasepool {
            publishTexture.release();

            if (server != nil) {
                [server stop];
#if !__has_feature(objc_arc)
                [(id)server release];
#endif
                server = nil;
            }
        }
    }

private:
    id<OsciSyphonOpenGLServer> server = nil;
    SyphonPublishTexture publishTexture;
};

class SyphonOpenGLReceiver final : public OpenGLReceiverImpl {
public:
    ~SyphonOpenGLReceiver() override {
        disconnect();
    }

    std::vector<SourceInfo> listSources() override {
        @autoreleasepool {
            std::vector<SourceInfo> sources;

            if (!loadSyphonFramework()) {
                return sources;
            }

            for (NSDictionary<NSString*, id>* description in syphonServerDescriptions(true)) {
                SourceInfo source = sourceInfoFromDescription(description);
                if (source.connectable) {
                    sources.push_back(std::move(source));
                }
            }

            return sources;
        }
    }

    ErrorCode connect(SourceInfo source) override {
        @autoreleasepool {
            disconnect();

            if (!loadSyphonFramework()) {
                return ErrorCode::sdkUnavailable;
            }

            NSDictionary<NSString*, id>* description = findDescriptionForSource(source);
            if (description == nil) {
                return ErrorCode::sourceNotFound;
            }

            CGLContextObj context = CGLGetCurrentContext();
            if (context == nullptr) {
                return ErrorCode::graphicsContextMismatch;
            }

            Class clientClass = NSClassFromString(@"SyphonOpenGLClient");
            if (clientClass == Nil) {
                return ErrorCode::sdkUnavailable;
            }

            id<OsciSyphonOpenGLClient> allocatedClient = (id<OsciSyphonOpenGLClient>)[clientClass alloc];
            client = [allocatedClient initWithServerDescription:description context:context options:nil newFrameHandler:nil];
            if (client == nil || ![client isValid]) {
                disconnect();
                return ErrorCode::connectionLost;
            }

            connectedSource = sourceInfoFromDescription(description);
            return ErrorCode::none;
        }
    }

    ErrorCode receive(ReceivedOpenGLTexture& frame) override {
        @autoreleasepool {
            if (client == nil || ![client isValid]) {
                return ErrorCode::connectionLost;
            }

            const bool hasNewFrame = [client hasNewFrame];
            id<OsciSyphonOpenGLImage> image = hasNewFrame || currentImage == nil ? [client newFrameImage] : currentImage;
            if (image == nil) {
                return ErrorCode::receiveFailed;
            }

            if (image != currentImage) {
                releaseCurrentImage();
                currentImage = image;
            }

            const GLuint textureName = [currentImage textureName];
            NSSize imageSize = [currentImage textureSize];
            const int textureWidth = juce::jmax(0, juce::roundToInt(static_cast<float>(imageSize.width)));
            const int textureHeight = juce::jmax(0, juce::roundToInt(static_cast<float>(imageSize.height)));

            frame.source = connectedSource;
            frame.texture.textureId = static_cast<std::uint32_t>(textureName);
            frame.texture.textureTarget = openGLTextureRectangle;
            frame.texture.originX = 0;
            frame.texture.originY = 0;
            frame.texture.width = textureWidth;
            frame.texture.height = textureHeight;
            if (hasNewFrame) {
                receivedFrameIndex++;
            }
            frame.texture.verticallyFlipped = false;
            frame.texture.frameIndex = receivedFrameIndex;
            frame.newFrame = hasNewFrame;

            return ErrorCode::none;
        }
    }

    void disconnect() override {
        @autoreleasepool {
            releaseCurrentImage();

            if (client != nil) {
                [client stop];
#if !__has_feature(objc_arc)
                [(id)client release];
#endif
                client = nil;
            }

            connectedSource = {};
            receivedFrameIndex = 0;
        }
    }

private:
    void releaseCurrentImage() {
        if (currentImage != nil) {
#if !__has_feature(objc_arc)
            [(id)currentImage release];
#endif
            currentImage = nil;
        }
    }

    id<OsciSyphonOpenGLClient> client = nil;
    id<OsciSyphonOpenGLImage> currentImage = nil;
    SourceInfo connectedSource;
    std::uint64_t receivedFrameIndex = 0;
};

} // namespace

BackendStatus getPlatformOpenGLBackendStatus() {
    @autoreleasepool {
        return syphonStatus();
    }
}

std::unique_ptr<OpenGLSenderImpl> createPlatformOpenGLSender() {
    return std::make_unique<SyphonOpenGLSender>();
}

std::unique_ptr<OpenGLReceiverImpl> createPlatformOpenGLReceiver() {
    return std::make_unique<SyphonOpenGLReceiver>();
}

} // namespace osci::texture

#endif
