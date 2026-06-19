// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alexander Salas Bastidas <ajsb85@firechip.dev>
//
// rive_d2d.cpp — Direct2D + WIC implementation of the Rive Factory / Renderer.
#include "rive_d2d.hpp"

#include <d2d1helper.h>
#include <d2d1effects.h>   // CLSID_D2D1Blend, D2D1_BLEND_PROP_MODE
#include <algorithm>
#include <cstring>

using namespace rive;

namespace rivepeek {

// --- helpers ---------------------------------------------------------------

// Map a Rive blend mode to its Direct2D `Blend` effect equivalent. Returns false
// for `srcOver` (the normal compositing path, which needs no effect).
static bool mapBlend(BlendMode m, D2D1_BLEND_MODE& out) {
    switch (m) {
        case BlendMode::multiply:   out = D2D1_BLEND_MODE_MULTIPLY;    return true;
        case BlendMode::screen:     out = D2D1_BLEND_MODE_SCREEN;      return true;
        case BlendMode::overlay:    out = D2D1_BLEND_MODE_OVERLAY;     return true;
        case BlendMode::darken:     out = D2D1_BLEND_MODE_DARKEN;      return true;
        case BlendMode::lighten:    out = D2D1_BLEND_MODE_LIGHTEN;     return true;
        case BlendMode::colorDodge: out = D2D1_BLEND_MODE_COLOR_DODGE; return true;
        case BlendMode::colorBurn:  out = D2D1_BLEND_MODE_COLOR_BURN;  return true;
        case BlendMode::hardLight:  out = D2D1_BLEND_MODE_HARD_LIGHT;  return true;
        case BlendMode::softLight:  out = D2D1_BLEND_MODE_SOFT_LIGHT;  return true;
        case BlendMode::difference: out = D2D1_BLEND_MODE_DIFFERENCE;  return true;
        case BlendMode::exclusion:  out = D2D1_BLEND_MODE_EXCLUSION;   return true;
        case BlendMode::hue:        out = D2D1_BLEND_MODE_HUE;         return true;
        case BlendMode::saturation: out = D2D1_BLEND_MODE_SATURATION;  return true;
        case BlendMode::color:      out = D2D1_BLEND_MODE_COLOR;       return true;
        case BlendMode::luminosity: out = D2D1_BLEND_MODE_LUMINOSITY;  return true;
        case BlendMode::srcOver:    default:                          return false;
    }
}

static D2D1_COLOR_F toColorF(ColorInt c) {
    return D2D1::ColorF(colorRed(c) / 255.0f, colorGreen(c) / 255.0f,
                        colorBlue(c) / 255.0f, colorAlpha(c) / 255.0f);
}

static D2D1::Matrix3x2F toD2D(const Mat2D& m) {
    return D2D1::Matrix3x2F(m.xx(), m.xy(), m.yx(), m.yy(), m.tx(), m.ty());
}

// ===========================================================================
// D2DShader
// ===========================================================================
D2DShader::D2DShader(Kind kind, float a, float b, float c, float d,
                     const ColorInt colors[], const float stops[], size_t count)
    : m_kind(kind), m_x1(a), m_y1(b), m_x2(c), m_y2(d),
      m_colors(colors, colors + count), m_stops(stops, stops + count) {}

ComPtr<ID2D1Brush> D2DShader::brush(ID2D1RenderTarget* rt, float opacity) const {
    std::vector<D2D1_GRADIENT_STOP> gs(m_colors.size());
    for (size_t i = 0; i < m_colors.size(); ++i) {
        gs[i].position = m_stops[i];
        gs[i].color = toColorF(m_colors[i]);
    }
    ComPtr<ID2D1GradientStopCollection> coll;
    if (gs.empty() ||
        FAILED(rt->CreateGradientStopCollection(gs.data(), (UINT32)gs.size(),
                                                D2D1_GAMMA_2_2,
                                                D2D1_EXTEND_MODE_CLAMP, &coll))) {
        return nullptr;
    }
    ComPtr<ID2D1Brush> out;
    if (m_kind == Kind::linear) {
        ComPtr<ID2D1LinearGradientBrush> b;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(m_x1, m_y1),
                                                    D2D1::Point2F(m_x2, m_y2)),
                coll.Get(), &b))) {
            b->SetOpacity(opacity);
            out = b;
        }
    } else {
        const float r = m_x2; // radius stored in slot c
        ComPtr<ID2D1RadialGradientBrush> b;
        if (SUCCEEDED(rt->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F(m_x1, m_y1),
                                                    D2D1::Point2F(0, 0), r, r),
                coll.Get(), &b))) {
            b->SetOpacity(opacity);
            out = b;
        }
    }
    return out;
}

// ===========================================================================
// D2DPaint
// ===========================================================================
ComPtr<ID2D1Brush> D2DPaint::brush(ID2D1RenderTarget* rt, float opacity) const {
    if (m_shader) {
        return static_cast<const D2DShader*>(m_shader.get())->brush(rt, opacity);
    }
    ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(rt->CreateSolidColorBrush(toColorF(m_color), &b))) return nullptr;
    b->SetOpacity(opacity);
    return b;
}

ComPtr<ID2D1StrokeStyle> D2DPaint::strokeStyle(ID2D1Factory* factory) const {
    D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
    if (m_cap == StrokeCap::round) cap = D2D1_CAP_STYLE_ROUND;
    else if (m_cap == StrokeCap::square) cap = D2D1_CAP_STYLE_SQUARE;

    D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER;
    if (m_join == StrokeJoin::round) join = D2D1_LINE_JOIN_ROUND;
    else if (m_join == StrokeJoin::bevel) join = D2D1_LINE_JOIN_BEVEL;

    ComPtr<ID2D1StrokeStyle> style;
    factory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(cap, cap, cap, join, 4.0f,
                                    D2D1_DASH_STYLE_SOLID, 0.0f),
        nullptr, 0, &style);
    return style;
}

// ===========================================================================
// D2DImage
// ===========================================================================
ID2D1Bitmap* D2DImage::bitmap(ID2D1RenderTarget* rt) const {
    if (m_cache && m_cacheRT == rt) return m_cache.Get();
    m_cache.Reset();
    m_cacheRT = rt;
    auto props = D2D1::BitmapProperties(D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt->CreateBitmap(D2D1::SizeU((UINT32)m_Width, (UINT32)m_Height),
                     m_pixels.data(), (UINT32)(m_Width * 4), props, &m_cache);
    return m_cache.Get();
}

// ===========================================================================
// D2DPath
// ===========================================================================
static void replayInto(RawPath& dst, const RawPath& src) {
    for (auto [verb, pts] : src) {
        switch (verb) {
            case PathVerb::move: dst.move(pts[0]); break;
            case PathVerb::line: dst.line(pts[1]); break;
            case PathVerb::quad: dst.quad(pts[1], pts[2]); break;
            case PathVerb::cubic: dst.cubic(pts[1], pts[2], pts[3]); break;
            case PathVerb::close: dst.close(); break;
        }
    }
}

void D2DPath::addRenderPath(const RenderPath* path, const Mat2D& transform) {
    auto* other = static_cast<const D2DPath*>(path);
    replayInto(m_path, other->m_path.transform(transform));
    m_dirty = true;
}

void D2DPath::addRawPath(const RawPath& path) {
    replayInto(m_path, path);
    m_dirty = true;
}

ID2D1PathGeometry* D2DPath::geometry() const {
    if (m_geometry && !m_dirty) return m_geometry.Get();
    m_geometry.Reset();
    if (FAILED(m_factory->CreatePathGeometry(&m_geometry))) return nullptr;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(m_geometry->Open(&sink))) { m_geometry.Reset(); return nullptr; }
    sink->SetFillMode(m_fillRule == FillRule::evenOdd ? D2D1_FILL_MODE_ALTERNATE
                                                      : D2D1_FILL_MODE_WINDING);
    bool figureOpen = false;
    for (auto [verb, pts] : m_path) {
        switch (verb) {
            case PathVerb::move:
                if (figureOpen) sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y),
                                  D2D1_FIGURE_BEGIN_FILLED);
                figureOpen = true;
                break;
            case PathVerb::line:
                sink->AddLine(D2D1::Point2F(pts[1].x, pts[1].y));
                break;
            case PathVerb::quad:
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                    D2D1::Point2F(pts[1].x, pts[1].y),
                    D2D1::Point2F(pts[2].x, pts[2].y)));
                break;
            case PathVerb::cubic:
                sink->AddBezier(D2D1::BezierSegment(
                    D2D1::Point2F(pts[1].x, pts[1].y),
                    D2D1::Point2F(pts[2].x, pts[2].y),
                    D2D1::Point2F(pts[3].x, pts[3].y)));
                break;
            case PathVerb::close:
                if (figureOpen) {
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figureOpen = false;
                }
                break;
        }
    }
    if (figureOpen) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    m_dirty = false;
    return m_geometry.Get();
}

// ===========================================================================
// D2DFactory
// ===========================================================================
rcp<RenderBuffer> D2DFactory::makeRenderBuffer(RenderBufferType type,
                                               RenderBufferFlags flags,
                                               size_t size) {
    return make_rcp<D2DBuffer>(type, flags, size);
}

rcp<RenderShader> D2DFactory::makeLinearGradient(float sx, float sy, float ex,
                                                 float ey, const ColorInt colors[],
                                                 const float stops[], size_t n) {
    return make_rcp<D2DShader>(D2DShader::Kind::linear, sx, sy, ex, ey, colors,
                               stops, n);
}

rcp<RenderShader> D2DFactory::makeRadialGradient(float cx, float cy, float radius,
                                                 const ColorInt colors[],
                                                 const float stops[], size_t n) {
    return make_rcp<D2DShader>(D2DShader::Kind::radial, cx, cy, radius, 0, colors,
                               stops, n);
}

rcp<RenderPath> D2DFactory::makeRenderPath(RawPath& rp, FillRule fr) {
    RawPath local;
    local.swap(rp); // we are allowed to consume the caller's RawPath
    return make_rcp<D2DPath>(m_d2d, std::move(local), fr);
}

rcp<RenderPath> D2DFactory::makeEmptyRenderPath() {
    return make_rcp<D2DPath>(m_d2d);
}

rcp<RenderPaint> D2DFactory::makeRenderPaint() { return make_rcp<D2DPaint>(); }

rcp<RenderImage> D2DFactory::decodeImage(Span<const uint8_t> bytes) {
    if (!m_wic || bytes.empty()) return nullptr;

    ComPtr<IWICStream> stream;
    if (FAILED(m_wic->CreateStream(&stream))) return nullptr;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),
                                            (DWORD)bytes.size())))
        return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(m_wic->CreateDecoderFromStream(stream.Get(), nullptr,
                                              WICDecodeMetadataCacheOnLoad,
                                              &decoder)))
        return nullptr;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(m_wic->CreateFormatConverter(&conv))) return nullptr;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)))
        return nullptr;

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (w == 0 || h == 0) return nullptr;

    std::vector<uint8_t> pixels((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)pixels.size(),
                                pixels.data())))
        return nullptr;

    return make_rcp<D2DImage>((int)w, (int)h, std::move(pixels));
}

// ===========================================================================
// D2DRenderer
// ===========================================================================
D2DRenderer::D2DRenderer(ID2D1RenderTarget* rt, ID2D1Factory* factory)
    : m_rt(rt), m_factory(factory) {
    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // A device context (D2D1.1+) is needed for the Blend effect used by
    // non-srcOver blend modes. Both the Hwnd and WIC-bitmap render targets
    // expose it on Win8+; if not, blends transparently degrade to srcOver.
    m_rt->QueryInterface(IID_PPV_ARGS(m_dc.GetAddressOf()));
}

void D2DRenderer::save() {
    m_stack.push_back(m_state);
    m_state.clipLayers = 0;
}

void D2DRenderer::restore() {
    for (int i = 0; i < m_state.clipLayers; ++i) {
        m_rt->PopLayer();
        if (!m_clips.empty()) m_clips.pop_back();
    }
    if (!m_stack.empty()) {
        m_state = m_stack.back();
        m_stack.pop_back();
    }
}

void D2DRenderer::transform(const Mat2D& m) {
    m_state.transform = toD2D(m) * m_state.transform;
}

void D2DRenderer::modulateOpacity(float opacity) { m_state.opacity *= opacity; }

void D2DRenderer::clipPath(RenderPath* path) {
    auto* geo = static_cast<D2DPath*>(path)->geometry();
    if (!geo) return;
    ComPtr<ID2D1Layer> layer;
    if (FAILED(m_rt->CreateLayer(nullptr, &layer))) return;
    applyTransform();
    m_rt->PushLayer(
        D2D1::LayerParameters(D2D1::InfiniteRect(), geo,
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                              D2D1::Matrix3x2F::Identity(), 1.0f, nullptr,
                              D2D1_LAYER_OPTIONS_NONE),
        layer.Get());
    m_state.clipLayers++;
    m_clips.push_back({geo, m_state.transform});
}

void D2DRenderer::drawPath(RenderPath* path, RenderPaint* paint) {
    auto* p = static_cast<D2DPath*>(path);
    auto* pt = static_cast<D2DPaint*>(paint);
    auto* geo = p->geometry();
    if (!geo) return;

    // The geometry and stroke style come from the factory (device-independent),
    // so this can draw onto either m_rt or an offscreen blend target; the brush
    // is rebuilt per target inside the lambda.
    const bool fill = pt->style() == RenderPaintStyle::fill;
    auto emit = [&](ID2D1RenderTarget* tgt) {
        auto brush = pt->brush(tgt, m_state.opacity);
        if (!brush) return;
        if (fill) {
            tgt->FillGeometry(geo, brush.Get());
        } else {
            auto ss = pt->strokeStyle(m_factory);
            tgt->DrawGeometry(geo, brush.Get(), pt->thickness(), ss.Get());
        }
    };

    D2D1_BLEND_MODE bm;
    if (m_dc && mapBlend(pt->blendMode(), bm)) {
        blendComposite(emit, bm);
    } else {
        applyTransform();
        emit(m_rt);
    }
}

void D2DRenderer::drawImage(const RenderImage* image, ImageSampler sampler,
                            BlendMode blend, float opacity) {
    auto* img = static_cast<const D2DImage*>(image);
    const float w = (float)img->width(), h = (float)img->height();
    auto interp = sampler.filter == ImageFilter::nearest
                      ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                      : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    auto emit = [&](ID2D1RenderTarget* tgt) {
        auto* bmp = img->bitmap(tgt);
        if (!bmp) return;
        D2D1_RECT_F dst = D2D1::RectF(0, 0, w, h);
        tgt->DrawBitmap(bmp, dst, opacity * m_state.opacity, interp, nullptr);
    };

    D2D1_BLEND_MODE bm;
    if (m_dc && mapBlend(blend, bm)) {
        blendComposite(emit, bm);
    } else {
        applyTransform();
        emit(m_rt);
    }
}

void D2DRenderer::drawImageMesh(const RenderImage* image, ImageSampler sampler,
                                rcp<RenderBuffer> vertices_f32,
                                rcp<RenderBuffer> uvCoords_f32,
                                rcp<RenderBuffer> indices_u16,
                                uint32_t vertexCount, uint32_t indexCount,
                                BlendMode, float opacity) {
    auto* img = static_cast<const D2DImage*>(image);
    auto* bmp = img->bitmap(m_rt);
    if (!bmp) return;

    const float* verts = reinterpret_cast<const float*>(
        static_cast<D2DBuffer*>(vertices_f32.get())->data());
    const float* uvs = reinterpret_cast<const float*>(
        static_cast<D2DBuffer*>(uvCoords_f32.get())->data());
    const uint16_t* idx = reinterpret_cast<const uint16_t*>(
        static_cast<D2DBuffer*>(indices_u16.get())->data());
    if (!verts || !uvs || !idx) return;

    const float texW = (float)img->width();
    const float texH = (float)img->height();

    auto interp = sampler.filter == ImageFilter::nearest
                      ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                      : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    ComPtr<ID2D1BitmapBrush> brush;
    if (FAILED(m_rt->CreateBitmapBrush(
            bmp, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP,
                                             D2D1_EXTEND_MODE_CLAMP, interp),
            &brush)))
        return;
    brush->SetOpacity(opacity * m_state.opacity);
    applyTransform();

    for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
        uint16_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

        // Destination triangle (local space) and source triangle (texture px).
        D2D1_POINT_2F q0 = {verts[i0 * 2], verts[i0 * 2 + 1]};
        D2D1_POINT_2F q1 = {verts[i1 * 2], verts[i1 * 2 + 1]};
        D2D1_POINT_2F q2 = {verts[i2 * 2], verts[i2 * 2 + 1]};
        D2D1_POINT_2F s0 = {uvs[i0 * 2] * texW, uvs[i0 * 2 + 1] * texH};
        D2D1_POINT_2F s1 = {uvs[i1 * 2] * texW, uvs[i1 * 2 + 1] * texH};
        D2D1_POINT_2F s2 = {uvs[i2 * 2] * texW, uvs[i2 * 2 + 1] * texH};

        // Solve the affine A (2x2) + t with q = A*s + t from the two edge
        // vectors, then express it in D2D's row-vector form.
        float sx1 = s1.x - s0.x, sy1 = s1.y - s0.y;
        float sx2 = s2.x - s0.x, sy2 = s2.y - s0.y;
        float det = sx1 * sy2 - sx2 * sy1;
        if (det == 0.0f) continue;
        float inv = 1.0f / det;
        float dx1 = q1.x - q0.x, dy1 = q1.y - q0.y;
        float dx2 = q2.x - q0.x, dy2 = q2.y - q0.y;
        // A = D * S^{-1}
        float a = (dx1 * sy2 - dx2 * sy1) * inv;
        float b = (-dx1 * sx2 + dx2 * sx1) * inv;
        float c = (dy1 * sy2 - dy2 * sy1) * inv;
        float d = (-dy1 * sx2 + dy2 * sx1) * inv;
        float tx = q0.x - (a * s0.x + b * s0.y);
        float ty = q0.y - (c * s0.x + d * s0.y);
        brush->SetTransform(D2D1::Matrix3x2F(a, c, b, d, tx, ty));

        ComPtr<ID2D1PathGeometry> tri;
        if (FAILED(m_factory->CreatePathGeometry(&tri))) continue;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(tri->Open(&sink))) continue;
        sink->BeginFigure(q0, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(q1);
        sink->AddLine(q2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        m_rt->FillGeometry(tri.Get(), brush.Get());
    }
}

void D2DRenderer::blendComposite(
        const std::function<void(ID2D1RenderTarget*)>& emit, D2D1_BLEND_MODE mode) {
    // srcOver fallback, used whenever the blended path can't be set up. Leaves
    // output no worse than a renderer that never blended at all.
    auto fallback = [&] { applyTransform(); emit(m_rt); };
    if (!m_dc) { fallback(); return; }

    const D2D1_SIZE_U px = m_dc->GetPixelSize();
    if (px.width == 0 || px.height == 0) { fallback(); return; }
    float dpiX = 96.0f, dpiY = 96.0f;
    m_rt->GetDpi(&dpiX, &dpiY);
    const auto pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED);

    // 1) Snapshot the current backdrop into a standalone bitmap.
    m_dc->Flush();
    ComPtr<ID2D1Bitmap> backdrop;
    if (FAILED(m_dc->CreateBitmap(px, nullptr, 0,
            D2D1::BitmapProperties(pf, dpiX, dpiY), &backdrop)) ||
        FAILED(backdrop->CopyFromRenderTarget(nullptr, m_rt, nullptr))) {
        fallback();
        return;
    }

    // 2) Render the shape alone onto a transparent foreground target, on a
    //    secondary device context (the primary one is mid-BeginDraw and can't
    //    switch targets). Active clips are replayed so the blend stays confined
    //    to the clipped region.
    ComPtr<ID2D1Device> device;
    m_dc->GetDevice(&device);
    ComPtr<ID2D1DeviceContext> fx;
    if (!device ||
        FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &fx))) {
        fallback();
        return;
    }
    ComPtr<ID2D1Bitmap1> fg;
    if (FAILED(fx->CreateBitmap(px, nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pf, dpiX, dpiY),
            &fg))) {
        fallback();
        return;
    }
    fx->SetTarget(fg.Get());
    fx->SetDpi(dpiX, dpiY);
    fx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    fx->BeginDraw();
    fx->Clear(D2D1::ColorF(0, 0.0f));
    int pushed = 0;
    for (const auto& c : m_clips) {
        ComPtr<ID2D1Layer> layer;
        if (FAILED(fx->CreateLayer(nullptr, &layer))) break;
        fx->SetTransform(c.xform);
        fx->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), c.geo.Get(),
                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                          D2D1::Matrix3x2F::Identity(), 1.0f, nullptr,
                          D2D1_LAYER_OPTIONS_NONE),
                      layer.Get());
        ++pushed;
    }
    fx->SetTransform(m_state.transform);
    emit(fx.Get());
    for (int i = 0; i < pushed; ++i) fx->PopLayer();
    if (FAILED(fx->EndDraw())) { fallback(); return; }

    // 3) result = blend(backdrop, foreground), copied back over the target.
    //    Where the foreground is transparent the W3C blend reduces to the
    //    backdrop, so a straight copy leaves untouched pixels unchanged.
    ComPtr<ID2D1Effect> blend;
    if (FAILED(m_dc->CreateEffect(CLSID_D2D1Blend, &blend))) { fallback(); return; }
    blend->SetInput(0, backdrop.Get());
    blend->SetInput(1, fg.Get());
    blend->SetValue(D2D1_BLEND_PROP_MODE, mode);

    ComPtr<ID2D1Image> result;
    blend->GetOutput(&result);
    m_dc->SetTransform(D2D1::Matrix3x2F::Identity());
    m_dc->DrawImage(result.Get(), nullptr, nullptr,
                    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                    D2D1_COMPOSITE_MODE_SOURCE_COPY);
}

} // namespace rivepeek
