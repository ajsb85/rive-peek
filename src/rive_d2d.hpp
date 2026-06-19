// rive_d2d.hpp — A Direct2D + WIC backend for the Rive C++ runtime.
//
// Implements rive::Factory and rive::Renderer (plus the RenderPath / RenderPaint
// / RenderShader / RenderImage / RenderBuffer objects they produce) on top of
// Direct2D. This lets the core Rive runtime — compiled without its GPU "Rive
// Renderer" — draw into any ID2D1RenderTarget (a WIC bitmap target for headless
// thumbnails, or an HWND target inside the Explorer preview pane).
//
// Coordinate convention: rive::Mat2D maps (x',y') = (xx*x + yx*y + tx,
// xy*x + yy*y + ty), which is exactly D2D1::Matrix3x2F(xx, xy, yx, yy, tx, ty)
// under D2D's row-vector (p' = p * M) multiplication.
#pragma once

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

#include "rive/factory.hpp"
#include "rive/renderer.hpp"
#include "rive/math/raw_path.hpp"
#include "rive/math/mat2d.hpp"
#include "rive/shapes/paint/color.hpp"

namespace rivepeek {

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// RenderBuffer — raw byte storage for mesh vertex/uv/index data.
// ---------------------------------------------------------------------------
class D2DBuffer : public rive::RenderBuffer {
public:
    D2DBuffer(rive::RenderBufferType type, rive::RenderBufferFlags flags, size_t size)
        : rive::RenderBuffer(type, flags, size), m_data(size) {}

    const uint8_t* data() const { return m_data.data(); }

protected:
    void* onMap() override { return m_data.data(); }
    void onUnmap() override {}

private:
    std::vector<uint8_t> m_data;
};

// ---------------------------------------------------------------------------
// RenderShader — captures gradient parameters; the device-specific D2D brush is
// built lazily at draw time from the active render target.
// ---------------------------------------------------------------------------
class D2DShader : public rive::RenderShader {
public:
    enum class Kind { linear, radial };

    D2DShader(Kind kind, float a, float b, float c, float d,
              const rive::ColorInt colors[], const float stops[], size_t count);

    ComPtr<ID2D1Brush> brush(ID2D1RenderTarget* rt, float opacity) const;

private:
    Kind m_kind;
    float m_x1, m_y1, m_x2, m_y2; // linear: start/end ; radial: cx,cy,radius (in x2)
    std::vector<rive::ColorInt> m_colors;
    std::vector<float> m_stops;
};

// ---------------------------------------------------------------------------
// RenderPaint — fill/stroke style. Produces a concrete D2D brush + stroke style
// at draw time (brushes are bound to a specific render target).
// ---------------------------------------------------------------------------
class D2DPaint : public rive::RenderPaint {
public:
    void style(rive::RenderPaintStyle style) override { m_style = style; }
    void color(rive::ColorInt value) override { m_color = value; }
    void thickness(float value) override { m_thickness = value; }
    void join(rive::StrokeJoin value) override { m_join = value; }
    void cap(rive::StrokeCap value) override { m_cap = value; }
    void blendMode(rive::BlendMode value) override { m_blend = value; }
    void shader(rive::rcp<rive::RenderShader> sh) override { m_shader = std::move(sh); }
    void invalidateStroke() override {}

    rive::RenderPaintStyle style() const { return m_style; }
    rive::BlendMode blendMode() const { return m_blend; }
    float thickness() const { return m_thickness; }

    // Build the D2D brush for this paint, modulated by `opacity`.
    ComPtr<ID2D1Brush> brush(ID2D1RenderTarget* rt, float opacity) const;
    // Build the D2D stroke style (caps/joins) on the given factory.
    ComPtr<ID2D1StrokeStyle> strokeStyle(ID2D1Factory* factory) const;

private:
    rive::RenderPaintStyle m_style = rive::RenderPaintStyle::fill;
    rive::ColorInt m_color = 0xFF000000;
    float m_thickness = 1.0f;
    rive::StrokeJoin m_join = rive::StrokeJoin::miter;
    rive::StrokeCap m_cap = rive::StrokeCap::butt;
    rive::BlendMode m_blend = rive::BlendMode::srcOver;
    rive::rcp<rive::RenderShader> m_shader;
};

// ---------------------------------------------------------------------------
// RenderImage — decoded (premultiplied BGRA) pixels; the ID2D1Bitmap is created
// lazily and cached per render target.
// ---------------------------------------------------------------------------
class D2DImage : public rive::RenderImage {
public:
    D2DImage(int w, int h, std::vector<uint8_t> bgra)
        : m_pixels(std::move(bgra)) { m_Width = w; m_Height = h; }

    ID2D1Bitmap* bitmap(ID2D1RenderTarget* rt) const;

private:
    std::vector<uint8_t> m_pixels; // premultiplied BGRA, width*height*4
    mutable ComPtr<ID2D1Bitmap> m_cache;
    mutable ID2D1RenderTarget* m_cacheRT = nullptr;
};

// ---------------------------------------------------------------------------
// RenderPath — stores commands in a rive::RawPath; realizes an immutable
// ID2D1PathGeometry lazily and caches it until the path is mutated.
// ---------------------------------------------------------------------------
class D2DPath : public rive::RenderPath {
public:
    explicit D2DPath(ID2D1Factory* factory) : m_factory(factory) {}
    D2DPath(ID2D1Factory* factory, rive::RawPath path, rive::FillRule rule)
        : m_factory(factory), m_path(std::move(path)), m_fillRule(rule) {}

    // CommandPath
    void rewind() override { m_path.rewind(); m_dirty = true; }
    void fillRule(rive::FillRule value) override { m_fillRule = value; m_dirty = true; }
    void moveTo(float x, float y) override { m_path.moveTo(x, y); m_dirty = true; }
    void lineTo(float x, float y) override { m_path.lineTo(x, y); m_dirty = true; }
    void cubicTo(float ox, float oy, float ix, float iy, float x, float y) override {
        m_path.cubicTo(ox, oy, ix, iy, x, y); m_dirty = true;
    }
    void close() override { m_path.close(); m_dirty = true; }

    // RenderPath
    void addRenderPath(const rive::RenderPath* path, const rive::Mat2D& transform) override;
    void addRawPath(const rive::RawPath& path) override;

    rive::FillRule fillRuleValue() const { return m_fillRule; }
    ID2D1PathGeometry* geometry() const; // realizes + caches

private:
    ID2D1Factory* m_factory;
    rive::RawPath m_path;
    rive::FillRule m_fillRule = rive::FillRule::nonZero;
    mutable bool m_dirty = true;
    mutable ComPtr<ID2D1PathGeometry> m_geometry;
};

// ---------------------------------------------------------------------------
// Factory — produces the render objects above. Needs only an ID2D1Factory and a
// WIC factory; no live render target, so it works headlessly at File::import().
// ---------------------------------------------------------------------------
class D2DFactory : public rive::Factory {
public:
    D2DFactory(ID2D1Factory* d2d, IWICImagingFactory* wic) : m_d2d(d2d), m_wic(wic) {}

    rive::rcp<rive::RenderBuffer> makeRenderBuffer(rive::RenderBufferType,
                                                   rive::RenderBufferFlags,
                                                   size_t sizeInBytes) override;
    rive::rcp<rive::RenderShader> makeLinearGradient(float, float, float, float,
                                                     const rive::ColorInt[],
                                                     const float[], size_t) override;
    rive::rcp<rive::RenderShader> makeRadialGradient(float, float, float,
                                                     const rive::ColorInt[],
                                                     const float[], size_t) override;
    rive::rcp<rive::RenderPath> makeRenderPath(rive::RawPath&, rive::FillRule) override;
    rive::rcp<rive::RenderPath> makeEmptyRenderPath() override;
    rive::rcp<rive::RenderPaint> makeRenderPaint() override;
    rive::rcp<rive::RenderImage> decodeImage(rive::Span<const uint8_t>) override;

private:
    ID2D1Factory* m_d2d;
    IWICImagingFactory* m_wic;
};

// ---------------------------------------------------------------------------
// Renderer — drives an ID2D1RenderTarget from the rive::Renderer interface.
// ---------------------------------------------------------------------------
class D2DRenderer : public rive::Renderer {
public:
    D2DRenderer(ID2D1RenderTarget* rt, ID2D1Factory* factory);

    void save() override;
    void restore() override;
    void transform(const rive::Mat2D& m) override;
    void drawPath(rive::RenderPath* path, rive::RenderPaint* paint) override;
    void clipPath(rive::RenderPath* path) override;
    void drawImage(const rive::RenderImage*, rive::ImageSampler, rive::BlendMode,
                   float opacity) override;
    void drawImageMesh(const rive::RenderImage*, rive::ImageSampler,
                       rive::rcp<rive::RenderBuffer> vertices_f32,
                       rive::rcp<rive::RenderBuffer> uvCoords_f32,
                       rive::rcp<rive::RenderBuffer> indices_u16,
                       uint32_t vertexCount, uint32_t indexCount,
                       rive::BlendMode, float opacity) override;
    void modulateOpacity(float opacity) override;

private:
    struct State {
        D2D1::Matrix3x2F transform = D2D1::Matrix3x2F::Identity();
        float opacity = 1.0f;
        int clipLayers = 0; // layers pushed in this save scope
    };

    void applyTransform() { m_rt->SetTransform(m_state.transform); }

    ID2D1RenderTarget* m_rt;
    ID2D1Factory* m_factory;
    State m_state;
    std::vector<State> m_stack;
};

} // namespace rivepeek
