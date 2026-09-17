# generate_type_info

Generates the reflection tables from `BLK_PROPERTY` markers in the component
headers, so `type_info_generated.h` is produced from the real member
declarations instead of being hand-maintained beside them.

Outputs, all regenerated and never edited by hand:

| Project | Tables | Instances |
| --- | --- | --- |
| engine | `blk_engine/game/type_info_generated.h` | `blk_engine/game/type_info_generated.inl` |
| blaise | `blaise/src/game/blaise_gen.h` | `blaise/src/game/blaise_gen.inl` |

Both projects run it as a pre-build step, so editing a header is enough. It
rewrites a file only when the content changes, so it doesn't trigger rebuilds
on its own.

## Usage

```
python tools/type_info/generate_type_info.py            # every project
python tools/type_info/generate_type_info.py engine     # one project
python tools/type_info/generate_type_info.py --check    # exit 1 if an output is stale
```

Errors and warnings print in MSBuild's format, so they show up as build errors
pointing at the header line.

## Markers

`BLK_PROPERTY()` goes on its own line above a member of a
`BLK_DECLARE_COMPONENT` class; `BLK_ENUM()` goes inline after an enumerator.
Both are defined in `blk_core.h` and expand to nothing.

```cpp
BLK_PROPERTY()
f32 m_min_spawn_rate;                        // saved as "MinSpawnRate"

BLK_PROPERTY(SerializedAs = "LifeTime")
f32 m_StartingLifeTime;                      // the rule would say "StartingLifeTime"

enum ERenderPass {
    RP_FirstPerson BLK_ENUM(SerializedAs = "FirstPersonPass"),
    NUM_RENDER_PASSES BLK_ENUM(Skip)
};
```

The derivation rule is: drop `m_`, drop a Hungarian `p`/`b`, convert to
PascalCase, and capitalize a letter after a digit (`m_min_start_3d_offset` ->
`MinStart3DOffset`). Every key in the tree derives this way - the level files
were migrated to match - so `SerializedAs` exists for a key that shouldn't
follow its member, and nothing currently needs it.

An enum is reflected automatically when a property uses it. Its values are
saved **by position**, so its declaration order is on-disk format, and `Skip`
is only allowed on trailing enumerators.

`MinVal`/`MaxVal` bound a numeric property. The editor clamps to them when an
edit is committed - not per keystroke, so a half-typed number survives - and the
undo entry records the clamped value. Loading and saving ignore them, so a level
written before the bound existed keeps its value until someone edits the field.

```cpp
BLK_PROPERTY(MinVal = 1)
int m_grassCellsPerTerrainSide;
```

They apply to a single FLOAT or INT property: on anything else, including an
array of them, the generator errors, as it does when MinVal exceeds MaxVal.

Adding a specifier means adding it to `PROPERTY_SPECIFIERS`/`ENUM_SPECIFIERS`
in the script. Both a name the script doesn't know *and* a name nothing acts on
yet are build errors, so a specifier can never quietly do nothing.

## What keeps it honest

**The compiler checks every type the script reads.** Each `AddField` carries the
member's declared type as the parser saw it, and the macro `static_assert`s it
against `decltype(Class::member)`. The `BLK_TYPEINFO_*` tag is derived from that
type, so a misread declaration fails the build instead of registering a wrong
tag. This is why a ~700-line scanner is enough and libclang isn't needed: the
parser cannot silently be wrong about a type.

**The marker count is checked too.** The one thing the compiler can't see is a
marker the parser skipped, so anything it can't read as a single non-static data
member declaration - a C array, a bit-field, two declarators on one line - is an
error rather than a skip.

**Renaming a member renames the key.** The loader skips keys it doesn't
recognize (`File::ReadComponent`), so a rename drops that value from every
existing level, with no error. Migrate the three files under
`blaise/assets/levels/` in the same change - per component block, since the same
key name means different members on different components. `SerializedAs` is the
escape hatch when the data can't move.

## Limits worth knowing

- Every component needs at least one property: `TypeInfoHierarchyIterator`
  walks `GetTypeInfo()[i]` and dereferences `begin()` on an empty map. That's
  what the `m_Dummy*` members are for.
- Enums can't be arrays, and `std::vector<T>` of a primitive works only for the
  element types `NameToTypeInfoMap`'s constructor registers (`float`, `String`,
  `Vec4`); component element types register themselves.
- The parser tracks scopes and statements rather than parsing C++. It skips
  `#if 0` blocks and preprocessor lines, so a property hidden behind an
  `#ifdef` would be read as if it were always compiled.
