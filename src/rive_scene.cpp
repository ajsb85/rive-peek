// rive_scene.cpp
#include "rive_scene.hpp"

#include "rive/math/aabb.hpp"
#include "rive/layout.hpp"

using namespace rive;

namespace rivepeek {

bool RiveScene::load(const uint8_t* data, size_t size) {
    m_error.clear();
    m_artboard.reset();
    m_stateMachine.reset();
    m_animation.reset();
    m_file = nullptr;

    if (!data || size < 4) {
        m_error = "empty or truncated file";
        return false;
    }

    ImportResult result;
    m_file = File::import(Span<const uint8_t>(data, size), &m_factory, &result);
    if (result != ImportResult::success || !m_file) {
        switch (result) {
            case ImportResult::unsupportedVersion:
                m_error = "unsupported Rive version (runtime is major 7)";
                break;
            case ImportResult::malformed:
                m_error = "malformed .riv file";
                break;
            default:
                m_error = "failed to import .riv file";
                break;
        }
        return false;
    }

    m_artboard = m_file->artboardDefault();
    if (!m_artboard) {
        m_error = "file has no artboard";
        return false;
    }

    // Prefer the default state machine; fall back to the first animation.
    m_stateMachine = m_artboard->defaultStateMachine();
    if (!m_stateMachine && m_artboard->animationCount() > 0) {
        m_animation = m_artboard->animationAt(0);
    }

    // Settle the initial pose.
    advance(0.0f);
    return true;
}

float RiveScene::width() const {
    return m_artboard ? m_artboard->bounds().width() : 0.0f;
}
float RiveScene::height() const {
    return m_artboard ? m_artboard->bounds().height() : 0.0f;
}

void RiveScene::advance(float dt) {
    if (!m_artboard) return;
    if (m_stateMachine) {
        m_stateMachine->advanceAndApply(dt);
    } else if (m_animation) {
        m_animation->advanceAndApply(dt);
    } else {
        m_artboard->advance(dt);
    }
}

void RiveScene::draw(D2DRenderer& renderer, float frameW, float frameH) {
    if (!m_artboard) return;
    renderer.save();
    renderer.align(Fit::contain, Alignment::center,
                   AABB(0.0f, 0.0f, frameW, frameH), m_artboard->bounds());
    m_artboard->draw(&renderer);
    renderer.restore();
}

} // namespace rivepeek
