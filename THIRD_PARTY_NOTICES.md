# Texture Interop Notices

The Windows backend bundles unchanged Spout2 2.007.017 files from the upstream release archive `Spout-SDK-binaries_2-007-017_1.zip`:

- `third_party/spout2/2.007.017/include/SpoutLibrary/SpoutLibrary.h`
  - SHA-256: `5fc1a7bc8c204825fc16028a2ab8930db4e7ec2f6b6f99d39784f68046710e4b`
- `third_party/spout2/2.007.017/windows/x64/SpoutLibrary.dll`
  - SHA-256: `31f12268169397bacd9048903d9ad711fae58b788e90a860598b21b1daae62eb`

Source URL: https://github.com/leadedge/Spout2/releases/tag/2.007.017

The macOS backend bundles an unchanged Syphon framework for installer packaging:

- `third_party/syphon/5/macos/Syphon.framework`
  - `CFBundleShortVersionString`: `5`
  - Code signature: Developer ID Application: James Ball (D86A3M3H2L)
- `third_party/syphon/5/macos/Syphon.framework/Versions/A/Syphon`
  - SHA-256: `93f58e2ff47f9a74faf972d3e0bc738d1860f485a6374fcebb38e23b31ac3589`
- `third_party/syphon/5/macos/Syphon.framework/Versions/A/Resources/default.metallib`
  - SHA-256: `cb86a906b81d55f1c9bc76cca17daf283d68b212e9a596d102b11f3382524397`

The macOS backend loads `Syphon.framework` at runtime when it is available. Product installers can install the bundled framework to `/Library/Frameworks/Syphon.framework`.

Syphon source URL: https://github.com/Syphon/Syphon-Framework

The Windows Spout path uses the bundled `SpoutLibrary.dll` from the osci-render common application data folder. Product-facing notices must include the Spout2 license text, source URL, bundled file origin, and the checksum above.

## Syphon Framework License

Copyright 2010 bangnoise (Tom Butterworth) & vade (Anton Marini).
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

* Neither the name of the Syphon Project nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Spout2 License

BSD 2-Clause License

Copyright (c) 2016-2025, Lynn Jarvis
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
