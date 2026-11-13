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
    std::string Title;                      // UTF-8, empty when the module stores none;
                                            // Turbo Sound chips' titles as "first/second" if they differ
    std::string NoteTable;                  // zxtune's frequency table name, e.g. "ProTracker3.4_ASM"; joined like Title
    double FrameRate = 50;                  // frames per second
    double ChipClock = 0;                   // Hz; 0 when the module doesn't store it (only Vortex Tracker text does)
    std::vector<std::vector<Frame>> Chips;  // one per AY chip, two or three for Turbo Sound; all the same length
    std::vector<std::size_t> RowStarts;     // the frame each pattern row starts at, in play order
    std::size_t LoopFrame = 0;              // the frame the tune loops back to; 0 when it loops to its start
  };

  // Plays a module (PT3, PT2, STC, ASC, SQT, Vortex Tracker text, a Turbo Sound pair, ...) up to its
  // first loop; the chips of a Turbo Sound tune play for as long as its first chip does. Writes follow
  // zxtune's PSG dumper: values cut to the register width, unchanged values not repeated, except the
  // envelope shape, whose write restarts the envelope.
  // Empty when the data is not a module of a supported format.
  std::optional<Tune> Play(const void* data, std::size_t size);
}  // namespace UzxAY
