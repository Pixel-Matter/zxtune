/**
 *
 * @file
 *
 * @brief  uZX entry point implementation
 *
 **/

#include "uzx/ay_module.h"

#include "binary/container_factories.h"
#include "formats/chiptune/decoders.h"
#include "module/players/aym/all.h"
#include "module/players/aym/aym_base.h"
#include "module/players/aym/aym_factory.h"
#include "module/players/aym/protracker3.h"
#include "module/players/aym/ts.h"
#include "module/players/aym/turbosound.h"
#include "parameters/container.h"

#include <memory>
#include <utility>

namespace UzxAY
{
  namespace
  {
    namespace FC = Formats::Chiptune;
    namespace MA = Module::AYM;
    using Devices::AYM::Registers;

    // An hour of 50 Hz frames bounds a module that never loops.
    const std::size_t MAX_FRAMES = 50 * 60 * 60;

    // Single-chip tracker formats in zxtune's detection order (core/plugins/players/ay/aym_plugin.cpp).
    // Left out: memory dumps (AY, needs a Z80) and register streams (PSG, YM, VTX, AYC).
    class AnyModuleFactory : public Module::Factory
    {
    public:
      AnyModuleFactory()
      {
        Add(FC::CreateProTracker3Decoder(), Module::ProTracker3::CreateFactory(FC::ProTracker3::Parse));
        Add(FC::CreateVortexTracker2Decoder(),
            Module::ProTracker3::CreateFactory(FC::ProTracker3::VortexTracker2::Parse));
        Add(FC::CreateProTracker2Decoder(), &MA::CreateProTracker2Chiptune);
        Add(FC::CreateSoundTrackerCompiledDecoder(), &MA::CreateSoundTrackerCompiledChiptune);
        Add(FC::CreateSoundTrackerDecoder(), &MA::CreateSoundTrackerChiptune);
        Add(FC::CreateSoundTracker3Decoder(), &MA::CreateSoundTracker3Chiptune);
        Add(FC::CreateASCSoundMaster0xDecoder(), &MA::CreateASCSoundMaster0Chiptune);
        Add(FC::CreateASCSoundMaster1xDecoder(), &MA::CreateASCSoundMasterChiptune);
        Add(FC::CreateSoundTrackerProCompiledDecoder(), &MA::CreateSoundTrackerProChiptune);
        Add(FC::CreateProSoundMakerCompiledDecoder(), &MA::CreateProSoundMakerChiptune);
        Add(FC::CreateGlobalTrackerDecoder(), &MA::CreateGlobalTrackerChiptune);
        Add(FC::CreateProTracker1Decoder(), &MA::CreateProTracker1Chiptune);
        Add(FC::CreateSQTrackerDecoder(), &MA::CreateSQTrackerChiptune);
        Add(FC::CreateProSoundCreatorDecoder(), &MA::CreateProSoundCreatorChiptune);
        Add(FC::CreateFastTrackerDecoder(), &MA::CreateFastTrackerChiptune);
      }

      Module::Holder::Ptr CreateModule(const Parameters::Accessor& params, const Binary::Container& data,
                                       Parameters::Container::Ptr properties) const override
      {
        for (const auto& player : Players)
        {
          if (player.Decoder->Check(data))
          {
            if (auto holder = player.Factory->CreateModule(params, data, properties))
            {
              LastFormat = player.Decoder->GetDescription();
              return holder;
            }
          }
        }
        return {};
      }

      mutable std::string LastFormat;

    private:
      void Add(FC::Decoder::Ptr decoder, Module::Factory::Ptr factory)
      {
        Players.push_back({std::move(decoder), std::move(factory)});
      }

      void Add(FC::Decoder::Ptr decoder, MA::ChiptuneCreator create)
      {
        Add(std::move(decoder), MA::CreateModuleFactory(create));
      }

      struct Player
      {
        FC::Decoder::Ptr Decoder;
        Module::Factory::Ptr Factory;
      };
      std::vector<Player> Players;
    };

    uint8_t WidthMask(Registers::Index reg)
    {
      switch (reg)
      {
      case Registers::TONEA_H:
      case Registers::TONEB_H:
      case Registers::TONEC_H:
      case Registers::ENV:
        return 0x0f;
      case Registers::TONEN:
      case Registers::VOLA:
      case Registers::VOLB:
      case Registers::VOLC:
        return 0x1f;
      default:
        return 0xff;
      }
    }

    // Same writes as devices/aym/dumper/dump_builder.cpp (OptimizedRenderState), every frame kept.
    class Recorder
    {
    public:
      void Add(const Registers& regs)
      {
        Frame frame;
        for (Registers::IndicesIterator it(regs); it; ++it)
        {
          const auto reg = *it;
          const uint8_t value = regs[reg] & WidthMask(reg);
          if (reg != Registers::ENV && Last.Has(reg) && Last[reg] == value)
          {
            continue;
          }
          Last[reg] = value;
          frame.Registers[reg] = value;
          frame.Written |= 1 << reg;
        }
        Frames.push_back(frame);
      }

      std::vector<Frame> Frames;

    private:
      Registers Last;
    };

    template<class Iterator, class OnFrame>
    void PlayToLoop(Iterator& iterator, OnFrame onFrame)
    {
      for (std::size_t frame = 0; !iterator.GetState().LoopCount && frame != MAX_FRAMES; ++frame)
      {
        onFrame(iterator.GetData());
        iterator.NextFrame();
      }
    }

    double FrameRate(Time::Microseconds frameDuration)
    {
      return 1e6 / static_cast<double>(frameDuration.Get());
    }

    std::optional<Tune> Record(const Module::Holder& holder, std::string format)
    {
      Tune result;
      result.Format = std::move(format);
      if (const auto* single = dynamic_cast<const MA::Holder*>(&holder))
      {
        const auto chiptune = single->GetChiptune();
        const auto iterator = chiptune->CreateDataIterator(MA::TrackParameters::Create(chiptune->GetProperties()));
        Recorder chip;
        PlayToLoop(*iterator, [&chip](const Registers& regs) { chip.Add(regs); });
        result.FrameRate = FrameRate(chiptune->GetFrameDuration());
        result.Chips.push_back(std::move(chip.Frames));
      }
      else if (const auto* pair = dynamic_cast<const Module::TurboSound::Holder*>(&holder))
      {
        const auto& chiptune = pair->GetChiptune();
        const auto props = chiptune.GetProperties();
        const auto iterator =
            chiptune.CreateDataIterator(MA::TrackParameters::Create(props, 0), MA::TrackParameters::Create(props, 1));
        Recorder chips[2];
        PlayToLoop(*iterator, [&chips](const Devices::TurboSound::Registers& regs) {
          chips[0].Add(regs[0]);
          chips[1].Add(regs[1]);
        });
        result.FrameRate = FrameRate(chiptune.GetFrameDuration());
        result.Chips.push_back(std::move(chips[0].Frames));
        result.Chips.push_back(std::move(chips[1].Frames));
      }
      if (result.Chips.empty() || result.Chips.front().empty())
      {
        return {};
      }
      return result;
    }
  }  // namespace

  std::optional<Tune> Play(const void* data, std::size_t size)
  {
    try
    {
      const auto container = Binary::CreateContainer(Binary::View(data, size));
      if (!container)  // no data
      {
        return {};
      }
      const auto params = Parameters::Container::Create();

      // Turbo Sound first, as in zxtune: two modules of any format above, then a "02TS" footer.
      const auto turboSound = FC::CreateTurboSoundDecoder();
      if (turboSound->Check(*container))
      {
        auto submodules = std::make_unique<AnyModuleFactory>();
        const auto* formats = submodules.get();
        const auto factory = Module::TS::CreateFactory(std::move(submodules));
        if (const auto holder = factory->CreateModule(*params, *container, Parameters::Container::Create()))
        {
          return Record(*holder, std::string(turboSound->GetDescription()) + " (" + formats->LastFormat + ")");
        }
      }

      const AnyModuleFactory modules;
      if (const auto holder = modules.CreateModule(*params, *container, Parameters::Container::Create()))
      {
        return Record(*holder, modules.LastFormat);
      }
    }
    catch (...)
    {
      // zxtune throws Error (not a std::exception) on some malformed data: not a module either.
    }
    return {};
  }
}  // namespace UzxAY
