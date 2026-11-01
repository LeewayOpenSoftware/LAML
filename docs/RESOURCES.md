# Resource lookup

`src/resources` resolves `{StaticResource}` / `{ThemeResource}` style keys against a tree of
dictionaries. It follows the publicly documented XAML rules; the MIT-licensed Microsoft.UI.Xaml
sources were used as a behavioural reference only and nothing was copied (see PROVENANCE.md).

Lookup order for `ResourceDictionary::Lookup(key, theme, global, scope)`:

1. the dictionary's own entries;
2. `MergedDictionaries`, **last to first**, each searched recursively with the same rules;
3. the dictionary's own `ThemeDictionaries`: the high-contrast variant when one is active
   (`HighContrastBlack|White|Custom`, then `HighContrast`; a subtree that requests Light/Dark
   uses the White/Black one), then `Light` / `Dark`, then `Default`;
4. the global theme dictionaries (system resources), where `Application.Resources` overrides
   what is found. This step is reached once, after the merged dictionaries.

Keys come in two spaces: named keys and implicit (type) keys, which never collide.
`Scope` flags (`kLocalOnly`, `kMerged`, `kLocalTheme`, `kGlobalTheme`) restrict the search.

Safety: `AddMerged` and `SetThemeDictionary` refuse cycles, duplicates and a merged subtree deeper
than `kMaxMergeDepth` (64); lookups visit at most that many levels, so a chain built by appending
at the tail cannot hang or overflow the stack - a key deeper than the bound is simply not found.

Not here yet: deferred resources, a not-found cache, theme-change notifications, ThemeResource
expressions, implicit style application. Values are plain (bool, integer, double, string) until
the object model exists.
