# Sandboxed API Integrations/Contributions

This directory contains reusable Sandboxed API integrations with external
libraries.

## Projects Sandboxed

Directory    | Project                                                           | Home Page                                                                            | Integration
------------ | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------ | -----------
`c-blosc/`   | c-blosc - A blocking, shuffling and loss-less compression library | [github.com/Blosc/c-blosc](https://github.com/Blosc/c-blosc)                         | CMake
`hunspell/`  | Hunspell - The most popular spellchecking library                 | [github.com/hunspell/hunspell](https://github.com/hunspell/hunspell)                 | CMake
`jsonnet/`   | Jsonnet - The Data Templating Language                            | [github.com/google/jsonnet](https://github.com/google/jsonnet)                       | CMake
`pffft/`     | PFFFT - a pretty fast Fourier Transform                           | [bitbucket.org/jpommier/pffft.git](https://bitbucket.org/jpommier/pffft.git)         | CMake
`zopfli`     | Zopfli - Compression Algorithm                                    | [github.com/google/zopfli](https://github.com/google/zopfli)                         | CMake
`zstd/`      | Zstandard - Fast real-time compression algorithm                  | [github.com/facebook/zstd](https://github.com/facebook/zstd)                         | CMake
`libidn2/`   | libidn2 - GNU IDN library                                         | [www.gnu.org/software/libidn/#libidn2](https://www.gnu.org/software/libidn/#libidn2) | CMake
`turbojpeg/` | High-level JPEG library                                           | [libjpeg-turbo.org/About/TurboJPEG](https://libjpeg-turbo.org/About/TurboJPEG)       | CMake

## Projects Shipping with Sandboxed API Sandboxes

Project                                 | Home Page                                                        | Integration
--------------------------------------- | ---------------------------------------------------------------- | -----------
YARA - The pattern matching swiss knife | [github.com/VirusTotal/yara](https://github.com/VirusTotal/yara) | Bazel
