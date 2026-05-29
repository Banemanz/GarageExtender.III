# GTA III Garage Extender
---

## Features

### Quadrupled vanilla safehouse capacity

The three original save garages keep their vanilla behavior, but their stored-car capacity is increased: Can be configured in ini, there is a 24 cap to number of cars per garage.

| Garage type | Vanilla capacity | Extended capacity |
| --- | ---: | ---: |
| `GARAGE_TYPE_PORTLAND_HIDEOUT` | 1 | 4 |
| `GARAGE_TYPE_STAUNTON_HIDEOUT` | 2 | 8 |
| `GARAGE_TYPE_SHORESIDE_HIDEOUT` | 3 | 12 |

### Extra safehouse garage types

The plugin adds nine additional savehouse garage type values in the CPP:

```cpp
enum eGta3SaveGarageType : unsigned char {
    GARAGE_TYPE_EXTRA_HIDEOUT_1 = 22,
    GARAGE_TYPE_EXTRA_HIDEOUT_2 = 23,
    GARAGE_TYPE_EXTRA_HIDEOUT_3 = 24,
    GARAGE_TYPE_EXTRA_HIDEOUT_4 = 25,
    GARAGE_TYPE_EXTRA_HIDEOUT_5 = 26,
    GARAGE_TYPE_EXTRA_HIDEOUT_6 = 27,
    GARAGE_TYPE_EXTRA_HIDEOUT_7 = 28,
    GARAGE_TYPE_EXTRA_HIDEOUT_8 = 29,
    GARAGE_TYPE_EXTRA_HIDEOUT_9 = 30
    ...ect
};
```

Each extra hideout gets an independent storage bank and uses the extended Shoreside-style capacity of **12 stored vehicles**.

Optional helper ID aliases are also provided for projects that add these garages after the vanilla garage list:

```cpp
GARAGE_ID_EXTRA_HIDEOUT_1
GARAGE_ID_EXTRA_HIDEOUT_2
GARAGE_ID_EXTRA_HIDEOUT_3
GARAGE_ID_EXTRA_HIDEOUT_4
GARAGE_ID_EXTRA_HIDEOUT_5
GARAGE_ID_EXTRA_HIDEOUT_6
GARAGE_ID_EXTRA_HIDEOUT_7
GARAGE_ID_EXTRA_HIDEOUT_8
GARAGE_ID_EXTRA_HIDEOUT_9
    ...ect
```

These ID aliases are just helper constants. The game stores garage behavior by `m_nType`, not by these helper ID names.

---

## Requirements

- GTA III PC **1.0 US** executable.
- A plugin-sdk III project or plugin-sdk-derived ASI project.
- Common plugin-sdk include paths configured so these headers resolve directly:

```cpp
#include "plugin.h"
#include "CGarage.h"
#include "CGarages.h"
#include "CStoredCar.h"
```

---

## Adding an extra savehouse garage

Use one of the new `GARAGE_TYPE_EXTRA_HIDEOUT_*` values when creating or changing a garage.

If you need multiple custom savehouses, use a different type for each location:

```cpp
GARAGE_TYPE_EXTRA_HIDEOUT_1
GARAGE_TYPE_EXTRA_HIDEOUT_2
GARAGE_TYPE_EXTRA_HIDEOUT_3
// ... up to GARAGE_TYPE_EXTRA_HIDEOUT_29
```

Each type maps to its own save storage bank. Reusing the same extra type for multiple physical garages means those garages will share the same stored-car bank, which is usually not what you want.

---

## Save compatibility

The loader handles three layouts:

1. **Vanilla GTA III garage block** (`0x156C`).
2. **Base-expanded layout** from the earlier three-hideout capacity extension.
3. **Final expanded layout** with the nine extra safehouse banks.

Vanilla saves load with empty new slots. Saves made with the expanded layout require this plugin to preserve the extra garage data.

If you remove the plugin after saving cars into extra hideouts, vanilla GTA III will not understand the expanded garage block. Keep a backup save before testing.

---

## What the plugin patches

The source patches GTA III 1.0 US garage logic at runtime to:

- Retarget vanilla safehouse stored-car arrays into expanded plugin-owned storage.
- Increase the save/load stored-car loop count for the three vanilla hideouts.
- Redirect save/load to splice the 29 extra safehouse banks into the garage save block.
- Redirect hideout capacity/count helpers to support vanilla and extra hideout types.
- Redirect store/restore paths so each extra hideout stores vehicles in its own bank.
- Make extra hideout types run through the vanilla hideout update behavior safely.
- Include extra hideouts in `IsPointWithinHideOutGarage` checks.
- Close and store extra hideouts before saving.

All hardcoded addresses are for **GTA III 1.0 US**.

---

## Limitations / Notes

- This does **not** automatically place new garages in the map. Another plugin/script still needs to call `CGarages::AddOne` or `CGarages::ChangeGarageType` with the extra type values.
- The game still has the original `CGarages::aGarages` pool size of 32 garage structs. This plugin adds extra savehouse types/storage, not an unlimited world-garage pool.
- Use one unique extra type per new safehouse location if you want independent storage.
- Extra hideout types are internally routed through vanilla Shoreside hideout behavior during garage updates, then restored to their original type.
- Target version is GTA III **1.0 US** only unless you port the addresses.

---

## Recommended testing

Before shipping a mod that uses this:

1. Start a new game (Garages will break if using original game save, need to make new one)
2. Add one custom garage using `GARAGE_TYPE_EXTRA_HIDEOUT_1`.
3. Store a vehicle, save, quit, reload, and confirm it restores.
4. Repeat with multiple extra types to ensure each location has independent storage.
5. Test vanilla Portland, Staunton, and Shoreside garages to confirm their expanded capacities still work.
6. Back up saves before distributing test builds.

---

## License / Credits

This source is designed for plugin-sdk based GTA III mod projects. Keep plugin-sdk attribution intact when distributing projects that include plugin-sdk code or headers. Slopped by Baneman, code is open sourced.
