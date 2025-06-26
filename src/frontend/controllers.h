#pragma once

#include <SDL2/SDL.h>
#include <map>

#include "peripherals/controller.h"

// NOTE : This is leftover from when we used to map controllers manually. For the most
// part this isn't used anymore. We utilize SDL's functionality which maps a wide variety
// of known controllers from a database into a standard XBox-like controller layout. For
// the most part, we don't need to think about mapping controllers this way.

enum Analog
{
  JoystickX,
  JoystickY,
  TriggerLeft,
  TriggerRight,
};

struct InputMapping {
  std::map<unsigned, maple::Controller::Button> digital;
  std::map<unsigned, Analog> analog;
};

static const std::map<std::string, InputMapping> SDL2Joystick_SupportedInputs = {
  {
    "HuiJia  USB GamePad",
    InputMapping{
      .digital = {
        { 0,  maple::Controller::Button::A },
        { 1,  maple::Controller::Button::B },
        { 2,  maple::Controller::Button::X },
        { 3,  maple::Controller::Button::Y },
        { 9,  maple::Controller::Button::Start },
        { 12, maple::Controller::Button::DpadUp },
        { 14, maple::Controller::Button::DpadDown },
        { 15, maple::Controller::Button::DpadLeft },
        { 13, maple::Controller::Button::DpadRight },
      },
      .analog = {
        { 0, JoystickX },
        { 1, JoystickY },
        { 2, TriggerLeft },
        { 3, TriggerRight }
      }
    }
  },
};

// Mapping of SDL's game controller paradigm to Dreamcast controllers

static const std::map<SDL_GameControllerButton, maple::Controller::Button>
  sdl2_digital_to_penguin = {
    { SDL_CONTROLLER_BUTTON_DPAD_UP, maple::Controller::Button::DpadUp },
    { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, maple::Controller::Button::DpadRight },
    { SDL_CONTROLLER_BUTTON_DPAD_DOWN, maple::Controller::Button::DpadDown },
    { SDL_CONTROLLER_BUTTON_DPAD_LEFT, maple::Controller::Button::DpadLeft },
    { SDL_CONTROLLER_BUTTON_A, maple::Controller::Button::A },
    { SDL_CONTROLLER_BUTTON_B, maple::Controller::Button::B },
    { SDL_CONTROLLER_BUTTON_X, maple::Controller::Button::X },
    { SDL_CONTROLLER_BUTTON_Y, maple::Controller::Button::Y },
    { SDL_CONTROLLER_BUTTON_START, maple::Controller::Button::Start },
  };

static const std::map<SDL_GameControllerAxis, Analog> sdl2_axis_to_penguin = {
  { SDL_CONTROLLER_AXIS_LEFTX, JoystickX },
  { SDL_CONTROLLER_AXIS_LEFTY, JoystickY },
  { SDL_CONTROLLER_AXIS_TRIGGERLEFT, TriggerLeft },
  { SDL_CONTROLLER_AXIS_TRIGGERRIGHT, TriggerRight }
};
