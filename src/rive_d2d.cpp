// rive_d2d.cpp — Direct2D + WIC implementation of the Rive Factory / Renderer.
#include "rive_d2d.hpp"

#include <d2d1helper.h>
#include <algorithm>
#include <cstring>

using namespace rive;

namespace rivepeek {

// --- helpers ---------------------------------------------------------------

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
}

void D2DRenderer::save() {
    m_stack.push_back(m_state);
    m_state.clipLayers = 0;
}

void D2DRenderer::restore() {
    for (int i = 0; i < m_state.clipLayers; ++i) m_rt->PopLayer();
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
}

void D2DRenderer::drawPath(RenderPath* path, RenderPaint* paint) {
    auto* p = static_cast<D2DPath*>(path);
    auto* pt = static_cast<D2DPaint*>(paint);
    auto* geo = p->geometry();
    if (!geo) return;
    auto brush = pt->brush(m_rt, m_state.opacity);
    if (!brush) return;
    applyTransform();
    if (pt->style() == RenderPaintStyle::fill) {
        m_rt->FillGeometry(geo, brush.Get());
    } else {
        auto ss = pt->strokeStyle(m_factory);
        m_rt->DrawGeometry(geo, brush.Get(), pt->thickness(), ss.Get());
    }
}

void D2DRenderer::drawImage(const RenderImage* image, ImageSampler sampler,
                            BlendMode, float opacity) {
    auto* img = static_cast<const D2DImage*>(image);
    auto* bmp = img->bitmap(m_rt);
    if (!bmp) return;
    applyTransform();
    auto interp = sampler.filter == ImageFilter::nearest
                      ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                      : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    D2D1_RECT_F dst = D2D1::RectF(0, 0, (float)img->width(), (float)img->height());
    m_rt->DrawBitmap(bmp, dst, opacity * m_state.opacity, interp, nullptr);
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

} // namespace rivepeek
