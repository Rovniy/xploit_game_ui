# Сторонние компоненты и лицензии

Все компоненты рантайма разрешают коммерческое распространение и модификацию и не требуют раскрытия исходного кода продукта. Требования по указанию авторства выполняются включением этого файла и полных текстов лицензий в поставляемый пакет (`unity/Packages/com.xploit.game_ui/Third Party Notices.md`; собирается скриптом `tools/build.ps1` из `native/vcpkg_installed/x64-windows-static-md/share/*/copyright` и из NuGet-пакетов V8).

| Компонент | Репозиторий | Лицензия | Коммерческое распространение | Модификация | Указание авторства | Раскрытие исходников |
|---|---|---|---|---|---|---|
| V8 | https://github.com/v8/v8 | BSD-3-Clause | да | да | да (текст лицензии) | нет |
| V8 NuGet-сборка (pmed) | https://github.com/pmed/v8-nuget | BSD-3-Clause (скрипты); бинарники под лицензией V8 | да | да | да | нет |
| Skia (включая skparagraph, skshaper, skunicode) | https://github.com/google/skia | BSD-3-Clause | да | да | да | нет |
| lexbor | https://github.com/lexbor/lexbor | Apache-2.0 | да | да | да (NOTICE, если есть) | нет |
| Yoga | https://github.com/facebook/yoga | MIT | да | да | да | нет |
| HarfBuzz | https://github.com/harfbuzz/harfbuzz | MIT («Old MIT») | да | да | да | нет |
| ICU | https://github.com/unicode-org/icu | Unicode License v3 | да | да | да (текст лицензии) | нет |
| FreeType | https://gitlab.freedesktop.org/freetype/freetype | FTL (двойная с GPLv2; используется FTL) | да | да | да (упоминание FreeType в документации) | нет |
| libpng | https://github.com/pnggroup/libpng | PNG Reference Library License v2 | да | да | да | нет |
| libjpeg-turbo | https://github.com/libjpeg-turbo/libjpeg-turbo | IJG + BSD-3 + zlib | да | да | да | нет |
| libwebp | https://chromium.googlesource.com/webm/libwebp | BSD-3-Clause | да | да | да | нет |
| zlib | https://github.com/madler/zlib | zlib | да | да | нет (рекомендуется) | нет |
| D3D12 Memory Allocator | https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator | MIT | да | да | да | нет |
| SPIRV-Cross | https://github.com/KhronosGroup/SPIRV-Cross | Apache-2.0 | да | да | да | нет |
| Abseil (в составе V8) | https://github.com/abseil/abseil-cpp | Apache-2.0 | да | да | да | нет |
| GoogleTest (только тесты) | https://github.com/google/googletest | BSD-3-Clause | да | да | да | нет |
| nlohmann/json (только тесты и CLI) | https://github.com/nlohmann/json | MIT | да | да | да | нет |
| Unity Native Plugin API (заголовки) | поставляется с Unity Editor | Unity Companion License | только в проектах, зависящих от Unity | да | да | нет |

Копилефтные лицензии (GPL/LGPL/MPL) в поставляемом рантайме отсутствуют.

Рассмотрены и отклонены (см. [ADR-0001](ADR-0001-stack.md)): Servo (MPL-2.0), LibWeb (BSD-2), CEF (BSD-3), RmlUi (MIT), Blitz (MIT/Apache-2.0, stylo MPL-2.0).
