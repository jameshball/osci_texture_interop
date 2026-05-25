#include "osci_texture_interop.h"

#include "osci_texture_interop.cpp"

#if JUCE_MAC && OSCI_TEXTURE_INTEROP_ENABLE_SYPHON
#include "platform/osci_SyphonOpenGL.mm"
#endif
