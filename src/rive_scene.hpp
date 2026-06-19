// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alexander Salas Bastidas <ajsb85@firechip.dev>
//
// rive_scene.hpp — Loads a .riv into a drawable scene: imports the file, picks
// the default artboard and its default state machine (or first animation), and
// advances + draws it fit/centered into a target rectangle.
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include "rive_d2d.hpp"
#include "rive/file.hpp"
#include "rive/artboard.hpp"
#include "rive/animation/state_machine_instance.hpp"
#include "rive/animation/linear_animation_instance.hpp"

struct ID2D1Factory;
struct IWICImagingFactory;

namespace rivepeek {

class RiveScene {
public:
    RiveScene(ID2D1Factory* d2d, IWICImagingFactory* wic) : m_factory(d2d, wic) {}

    // Parse the .riv bytes and instantiate the default artboard. Returns false
    // and sets error() on failure.
    bool load(const uint8_t* data, size_t size);

    bool loaded() const { return m_artboard != nullptr; }
    const std::string& error() const { return m_error; }

    // Intrinsic artboard dimensions (for choosing an aspect-correct surface).
    float width() const;
    float height() const;

    // Advance the active scene by dt seconds (state machine or animation).
    void advance(float dt);

    // Draw fit/centered into a [0,0,frameW,frameH] rectangle of the target.
    void draw(D2DRenderer& renderer, float frameW, float frameH);

private:
    D2DFactory m_factory;
    rive::rcp<rive::File> m_file;
    std::unique_ptr<rive::ArtboardInstance> m_artboard;
    std::unique_ptr<rive::StateMachineInstance> m_stateMachine;
    std::unique_ptr<rive::LinearAnimationInstance> m_animation;
    std::string m_error;
};

} // namespace rivepeek
