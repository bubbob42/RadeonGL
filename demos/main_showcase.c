/*
 * main_showcase.c -- Showcase: Spinner, Glow and Landscape in turn in one
 * context, each for SCENESECS seconds (12) or SCENEFRAMES frames; the
 * space bar skips to the next. Every scene gives back its textures before
 * the next one makes its own.
 */

#include "demo.h"

const char demo_ver[] = "$VER: Showcase 0.1 (03.11.2026)" DEMO_CPU;

const struct Scene *const demo_scenes[] = { &scene_spinner, &scene_glow, &scene_landscape, NULL };
