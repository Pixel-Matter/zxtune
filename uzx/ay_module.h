/**
 *
 * @file
 *
 * @brief  uZX entry point: plays an AY tracker module and records the chip registers
 *
 **/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UzxAY
{
  struct Frame
  {
    std::array<uint8_t, 14> Registers = {};
    uint16_t Written = 0;  // bit N set: register N was written in this frame
  };

  struct Tune
  {
    std::string Format;                     // zxtune's format name, e.g. "Pro Tracker v3.x"
    double FrameRate = 50;                  // frames per second
    std::vector<std::vector<Frame>> Chips;  // one per AY chip, two for Turbo Sound; all the same length
  };

  // Plays a module (PT3, PT2, STC, ASC, SQT, Vortex Tracker text, a Turbo Sound pair, ...) up to its
  // first loop. Writes follow zxtune's PSG dumper: values cut to the register width, unchanged values
  // not repeated, except the envelope shape, whose write restarts the envelope.
  // Empty when the data is not a module of a supported format.
  std::optional<Tune> Play(const void* data, std::size_t size);
}  // namespace UzxAY
