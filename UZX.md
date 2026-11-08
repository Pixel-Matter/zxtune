# zxtune for uZX

This `uzx` branch of [Pixel-Matter/zxtune](https://github.com/Pixel-Matter/zxtune) is what the
[uZX](https://github.com/Pixel-Matter/uZX) repository builds as its `third_party/zxtune` submodule. uZX uses
it to open ZX Spectrum tracker modules: `uzx/ay_module.h` plays one (PT3, PT2, STC, ASC, SQT, Vortex
Tracker text, Turbo Sound pairs, ...) up to its loop and returns the AY registers of every frame.

It differs from upstream [vitamin-caig/zxtune](https://github.com/vitamin-caig/zxtune) in:

- **Trimmed** by `uzx-trim.sh` to `src/`, `include/`, fmt and the AY regression samples. Apps, other
  libraries and the other chips' players are not built, and dropping them keeps the submodule small.
- **`CMakeLists.txt`**: the `zxtune_aym` static library: the AY players plus what they link to.
  Only `uzx/ay_module.h` is public.
- **`uzx/`**: the API uZX calls.
- **Patches**, each a commit marked `uzx:`
  - standard C++20 `_id` and `_source` literal operators (the GNU forms don't build with MSVC);
  - fmt without consteval format strings (Apple clang 21);
  - iterators, not pointers, from `string_view` and `array` (MSVC's are classes), and
    `parameters/types.h` renamed `parameters/value_types.h`: MSVC looks for a quoted include in the
    folders of every file up the include stack, so `"types.h"` found it instead of `include/types.h`;
  - `TurboSound::Holder::GetChiptune()`, to read both chips of a Turbo Sound module.

## Updating to an upstream tag

```sh
git fetch upstream tag rNNNN --no-tags
git merge rNNNN              # files deleted here and changed upstream conflict: keep them deleted
./uzx-trim.sh                # drops what the merge brought back
git commit
```

Then in uZX, build and run the `TrackerModule` tests: they compare the registers with zxtune's
`regression/*.debugay` dumps. A link error names a symbol whose source belongs in `CMakeLists.txt`.
