#include "render/skia/TestFrame.h"

#include "core/Log.h"

#include <include/codec/SkCodec.h>
#include <include/codec/SkJpegDecoder.h>
#include <include/codec/SkPngDecoder.h>
#include <include/codec/SkWebpDecoder.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkData.h>
#include <include/core/SkFontMgr.h>
#include <include/core/SkImage.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkPoint.h>
#include <include/core/SkRRect.h>
#include <include/core/SkStream.h>
#include <include/core/SkString.h>
#include <include/core/SkSpan.h>
#include <include/core/SkTileMode.h>
#include <include/effects/SkGradient.h>
#include <include/encode/SkPngEncoder.h>
#include <include/ports/SkTypeface_win.h>
#include <modules/skparagraph/include/FontCollection.h>
#include <modules/skparagraph/include/Paragraph.h>
#include <modules/skparagraph/include/ParagraphBuilder.h>
#include <modules/skparagraph/include/ParagraphStyle.h>
#include <modules/skparagraph/include/TextStyle.h>
#include <modules/skunicode/include/SkUnicode_icu.h>

#include <algorithm>
#include <mutex>

namespace xgu::render {
namespace {

using namespace skia::textlayout;

std::once_flag g_codecsOnce;

sk_sp<FontCollection> fontCollection() {
    static sk_sp<FontCollection> collection = [] {
        auto fc = sk_make_sp<FontCollection>();
        sk_sp<SkFontMgr> mgr = SkFontMgr_New_DirectWrite();
        if (!mgr) {
            XGU_LOG_WARNING("TestFrame: DirectWrite font manager unavailable");
        }
        fc->setDefaultFontManager(mgr, "Segoe UI");
        fc->enableFontFallback();
        return fc;
    }();
    return collection;
}

sk_sp<SkUnicode> unicode() {
    static sk_sp<SkUnicode> instance = SkUnicodes::ICU::Make();
    return instance;
}

// Builds a small checkerboard, encodes it to PNG and decodes it again so the
// codec path is exercised the same way <img src="x.png"> will be.
sk_sp<SkImage> makeRoundTrippedImage() {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            *bitmap.getAddr32(x, y) = light ? SkPreMultiplyColor(SkColorSetARGB(255, 0xF5, 0xB8, 0x41))
                                            : SkPreMultiplyColor(SkColorSetARGB(255, 0xD9, 0x5D, 0x39));
        }
    }
    SkDynamicMemoryWStream stream;
    if (!SkPngEncoder::Encode(&stream, bitmap.pixmap(), SkPngEncoder::Options{})) {
        XGU_LOG_ERROR("TestFrame: PNG encode failed");
        return nullptr;
    }
    sk_sp<SkData> encoded = stream.detachAsData();
    std::unique_ptr<SkCodec> codec = SkPngDecoder::Decode(encoded, nullptr);
    if (!codec) {
        XGU_LOG_ERROR("TestFrame: PNG decode failed");
        return nullptr;
    }
    auto [image, result] = codec->getImage();
    if (result != SkCodec::kSuccess || !image) {
        XGU_LOG_ERROR("TestFrame: PNG getImage failed (%d)", static_cast<int>(result));
        return nullptr;
    }
    return image;
}

} // namespace

void registerImageCodecs() {
    std::call_once(g_codecsOnce, [] {
        SkCodecs::Register(SkPngDecoder::Decoder());
        SkCodecs::Register(SkJpegDecoder::Decoder());
        SkCodecs::Register(SkWebpDecoder::Decoder());
    });
}

DisplayList recordTestFrame(int widthPx, int heightPx, float dpr, uint64_t frameId) {
    registerImageCodecs();
    DisplayList out;
    if (widthPx <= 0 || heightPx <= 0 || dpr <= 0.0f) {
        return out;
    }

    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeIWH(widthPx, heightPx));
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(dpr, dpr);

    const float w = static_cast<float>(widthPx) / dpr;
    const float h = static_cast<float>(heightPx) / dpr;
    const float margin = std::min(w, h) * 0.08f;
    const float radius = std::min(w, h) * 0.06f;
    const SkRect panel = SkRect::MakeLTRB(margin, margin, w - margin, h - margin);
    const SkRRect rrect = SkRRect::MakeRectXY(panel, radius, radius);

    // Drop shadow (box-shadow: 0 6px 24px rgba(0,0,0,.55)).
    {
        SkPaint shadow;
        shadow.setAntiAlias(true);
        shadow.setColor(SkColorSetARGB(140, 0, 0, 0));
        shadow.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, margin * 0.45f));
        SkRRect offset = rrect;
        offset.offset(0, margin * 0.35f);
        canvas->drawRRect(offset, shadow);
    }
    // Panel background.
    {
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(SkColorSetARGB(238, 0x1E, 0x2A, 0x44));
        canvas->drawRRect(rrect, fill);
    }
    // Border.
    {
        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(2.0f);
        stroke.setColor(SkColorSetARGB(255, 0x5A, 0xC8, 0xFA));
        canvas->drawRRect(rrect, stroke);
    }

    const float pad = margin * 1.25f;
    const SkRect content = panel.makeInset(pad, pad);

    // Health-bar style gradient.
    {
        const float barHeight = std::max(8.0f, content.height() * 0.08f);
        const SkRect track = SkRect::MakeXYWH(content.left(), content.bottom() - barHeight, content.width(), barHeight);
        SkPaint trackPaint;
        trackPaint.setAntiAlias(true);
        trackPaint.setColor(SkColorSetARGB(255, 0x10, 0x16, 0x24));
        canvas->drawRRect(SkRRect::MakeRectXY(track, barHeight / 2, barHeight / 2), trackPaint);

        const SkRect fillRect = SkRect::MakeXYWH(track.left(), track.top(), track.width() * 0.75f, barHeight);
        const SkPoint pts[2] = {{fillRect.left(), 0}, {fillRect.right(), 0}};
        const SkColor4f colors[2] = {SkColor4f::FromColor(SkColorSetARGB(255, 0x2E, 0xE5, 0x9D)),
                                     SkColor4f::FromColor(SkColorSetARGB(255, 0x5A, 0xC8, 0xFA))};
        const SkGradient gradient(SkGradient::Colors(SkSpan<const SkColor4f>(colors, 2), SkTileMode::kClamp),
                                  SkGradient::Interpolation{});
        SkPaint fillPaint;
        fillPaint.setAntiAlias(true);
        fillPaint.setShader(SkShaders::LinearGradient(pts, gradient));
        canvas->drawRRect(SkRRect::MakeRectXY(fillRect, barHeight / 2, barHeight / 2), fillPaint);
    }

    // Image (PNG round trip) in the top-right corner of the content box.
    if (sk_sp<SkImage> image = makeRoundTrippedImage()) {
        const float size = std::min(content.width(), content.height()) * 0.28f;
        const SkRect dst = SkRect::MakeXYWH(content.right() - size, content.top(), size, size);
        SkPaint imagePaint;
        imagePaint.setAntiAlias(true);
        canvas->save();
        canvas->clipRRect(SkRRect::MakeRectXY(dst, size * 0.12f, size * 0.12f), true);
        canvas->drawImageRect(image, dst, SkSamplingOptions(SkFilterMode::kLinear), &imagePaint);
        canvas->restore();
    }

    // Text via skparagraph.
    {
        const float titlePx = std::clamp(content.height() * 0.22f, 18.0f, 72.0f);
        ParagraphStyle paragraphStyle;
        paragraphStyle.setTextAlign(TextAlign::kLeft);

        TextStyle title;
        title.setColor(SK_ColorWHITE);
        title.setFontFamilies({SkString("Segoe UI"), SkString("Arial")});
        title.setFontSize(titlePx);
        title.setFontStyle(SkFontStyle::Bold());

        TextStyle subtitle = title;
        subtitle.setFontSize(titlePx * 0.42f);
        subtitle.setFontStyle(SkFontStyle::Normal());
        subtitle.setColor(SkColorSetARGB(255, 0x9F, 0xB3, 0xC8));

        std::unique_ptr<ParagraphBuilder> builder = ParagraphBuilder::make(paragraphStyle, fontCollection(), unicode());
        builder->pushStyle(title);
        builder->addText("Hello World\n");
        builder->pop();
        builder->pushStyle(subtitle);
        builder->addText("xploit_game_ui \xC2\xB7 Skia Ganesh \xC2\xB7 Direct3D 12");
        builder->pop();
        std::unique_ptr<Paragraph> paragraph = builder->Build();
        paragraph->layout(content.width() * 0.68f);
        paragraph->paint(canvas, content.left(), content.top());
    }

    out.picture = recorder.finishRecordingAsPicture();
    out.dirtyPx = SkIRect::MakeWH(widthPx, heightPx);
    out.sizePx = SkISize::Make(widthPx, heightPx);
    out.devicePixelRatio = dpr;
    out.frameId = frameId;
    return out;
}

} // namespace xgu::render
