/**
 *
 * @file
 *
 * @brief  uZX entry point implementation
 *
 **/

#include "uzx/ay_module.h"

#include "binary/container_factories.h"
#include "core/core_parameters.h"
#include "formats/chiptune/decoders.h"
#include "module/players/aym/all.h"
#include "module/players/aym/aym_base.h"
#include "module/players/aym/aym_factory.h"
#include "module/players/aym/protracker3.h"
#include "module/players/aym/ts.h"
#include "module/attributes.h"
#include "module/players/aym/turbosound.h"
#include "parameters/container.h"
#include "strings/encoding.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
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

    // Calls onFrame with each frame's registers up to the loop; sets the tune's row starts and loop frame.
    template<class Iterator, class OnFrame>
    void PlayToLoop(Iterator& iterator, OnFrame onFrame, Tune& tune)
    {
      std::map<uint_t, std::size_t> positionStarts;  // the frame each order list position starts at
      for (std::size_t frame = 0; frame != MAX_FRAMES; ++frame)
      {
        const auto state = iterator.GetState();
        if (state.LoopCount)
        {
          // Looped back to the first row of the loop position
          if (const auto start = state.Track ? positionStarts.find(state.Track->Position) : positionStarts.end();
              start != positionStarts.end())
          {
            tune.LoopFrame = start->second;
          }
          break;
        }
        if (state.Track && state.Track->Quirk == 0)  // the first frame of a row
        {
          tune.RowStarts.push_back(frame);
          positionStarts.try_emplace(state.Track->Position, frame);
        }
        onFrame(iterator.GetData());
        iterator.NextFrame();
      }
    }

    std::string FindText(const Parameters::Accessor& props, Parameters::Identifier name)
    {
      String value;
      Parameters::FindValue(props, name, value);
      return value;
    }

    double FrameRate(Time::Microseconds frameDuration)
    {
      return 1e6 / static_cast<double>(frameDuration.Get());
    }

    // zxtune's rule for the strings of a Turbo Sound tune's chips: "first/second" if they differ.
    void Join(std::string& joined, const std::string& value, bool first)
    {
      if (first)
      {
        joined = value;
      }
      else if (joined != value)
      {
        joined += '/' + value;
      }
    }

    void ReadText(Tune& tune, const Parameters::Accessor& properties, bool first)
    {
      Join(tune.Title, Strings::ToAutoUtf8(FindText(properties, Module::ATTR_TITLE)), first);  // Vortex text keeps CP1251/866
      Join(tune.NoteTable, FindText(properties, Parameters::ZXTune::Core::AYM::TABLE), first);
    }

    std::optional<Tune> NotEmpty(Tune tune)
    {
      if (tune.Chips.front().empty())
      {
        return {};
      }
      return tune;
    }

    // Chips that play one tune: the first chip's rows and loop lead, as in zxtune's Turbo Sound.
    class Together
    {
    public:
      explicit Together(const std::vector<MA::Chiptune::Ptr>& chiptunes)
      {
        for (const auto& chiptune : chiptunes)
        {
          Chips.push_back(chiptune->CreateDataIterator(MA::TrackParameters::Create(chiptune->GetProperties())));
        }
      }

      auto GetState() const
      {
        return Chips.front()->GetState();
      }

      std::vector<Registers> GetData() const
      {
        std::vector<Registers> regs;
        for (const auto& chip : Chips)
        {
          regs.push_back(chip->GetData());
        }
        return regs;
      }

      void NextFrame()
      {
        for (const auto& chip : Chips)
        {
          chip->NextFrame();
        }
      }

    private:
      std::vector<MA::DataIterator::Ptr> Chips;
    };

    std::optional<Tune> Record(const std::vector<MA::Chiptune::Ptr>& chiptunes, std::string format)
    {
      Tune result;
      result.Format = std::move(format);
      Together together(chiptunes);
      std::vector<Recorder> chips(chiptunes.size());
      const auto record = [&chips](const std::vector<Registers>& regs) {
        for (std::size_t chip = 0; chip != chips.size(); ++chip)
        {
          chips[chip].Add(regs[chip]);
        }
      };
      PlayToLoop(together, record, result);
      result.FrameRate = FrameRate(chiptunes.front()->GetFrameDuration());
      for (std::size_t chip = 0; chip != chips.size(); ++chip)
      {
        ReadText(result, *chiptunes[chip]->GetProperties(), chip == 0);
        result.Chips.push_back(std::move(chips[chip].Frames));
      }
      return NotEmpty(std::move(result));
    }

    std::optional<Tune> Record(const Module::Holder& holder, std::string format)
    {
      if (const auto* single = dynamic_cast<const MA::Holder*>(&holder))
      {
        return Record({single->GetChiptune()}, std::move(format));
      }
      const auto* pair = dynamic_cast<const Module::TurboSound::Holder*>(&holder);
      if (!pair)
      {
        return {};
      }
      Tune result;
      result.Format = std::move(format);
      const auto& chiptune = pair->GetChiptune();
      const auto props = chiptune.GetProperties();
      const auto iterator =
          chiptune.CreateDataIterator(MA::TrackParameters::Create(props, 0), MA::TrackParameters::Create(props, 1));
      Recorder chips[2];
      const auto record = [&chips](const Devices::TurboSound::Registers& regs) {
        chips[0].Add(regs[0]);
        chips[1].Add(regs[1]);
      };
      PlayToLoop(*iterator, record, result);
      result.FrameRate = FrameRate(chiptune.GetFrameDuration());
      ReadText(result, *props, true);  // zxtune has joined the chips' strings
      result.Chips.push_back(std::move(chips[0].Frames));
      result.Chips.push_back(std::move(chips[1].Frames));
      return NotEmpty(std::move(result));
    }

    // Vortex Tracker's text modules: a Turbo Sound tune is one module per chip, one after another.
    constexpr std::string_view VORTEX_MODULE = "[Module]";

    std::vector<std::string_view> SplitVortexModules(std::string_view text)
    {
      std::vector<std::string_view> modules;
      for (std::size_t start = 0; start != std::string_view::npos;)
      {
        const auto next = text.find("\n" + std::string(VORTEX_MODULE), start);
        const auto end = next == std::string_view::npos ? text.size() : next + 1;
        modules.push_back(text.substr(start, end - start));
        start = next == std::string_view::npos ? next : end;
      }
      return modules;
    }

    // A number in a Vortex Tracker module's header, such as "ChipFreq=1750000"; 0 when there is none.
    double HeaderNumber(std::string_view module, std::string_view name)
    {
      const auto key = "\n" + std::string(name) + '=';
      const auto pos = module.find(key);
      if (pos == std::string_view::npos)
      {
        return 0;
      }
      return std::strtod(std::string(module.substr(pos + key.size(), 16)).c_str(), nullptr);
    }

    std::optional<Tune> PlayVortexText(std::string_view text, const AnyModuleFactory& formats)
    {
      const auto params = Parameters::Container::Create();
      std::vector<MA::Chiptune::Ptr> chiptunes;
      for (const auto module : SplitVortexModules(text))
      {
        const auto data = Binary::CreateContainer(Binary::View(module.data(), module.size()));
        const auto holder = data ? formats.CreateModule(*params, *data, Parameters::Container::Create()) : nullptr;
        const auto* single = dynamic_cast<const MA::Holder*>(holder.get());
        if (!single)
        {
          return {};
        }
        chiptunes.push_back(single->GetChiptune());
      }
      auto tune = Record(chiptunes, formats.LastFormat);
      if (!tune)
      {
        return {};
      }
      // Vortex Tracker 2.5 and later store the clock and interrupt rate the tune was written for (IntFreq in mHz).
      tune->ChipClock = HeaderNumber(text, "ChipFreq");
      if (const auto interruptRate = HeaderNumber(text, "IntFreq"); interruptRate > 0)
      {
        tune->FrameRate = interruptRate / 1000;
      }
      return tune;
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
      if (const std::string_view text(static_cast<const char*>(data), size); text.starts_with(VORTEX_MODULE))
      {
        return PlayVortexText(text, modules);
      }
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
