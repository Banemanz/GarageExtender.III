// GTA III (1.0 US) - expanded save-garage storage and extra safehouse garage types.
// Build as a plugin-sdk III ASI/DLL source file.
//
// Verified against the repository's plugin-sdk declarations and the IDB dump:
// - CGarages::Save    0x4284E0 stores 6 interleaved safehouse slots and returns size 0x156C.
// - CGarages::Load    0x428940 loads the same layout.
// - CGarage::RestoreCarsForThisHideOut 0x427A40 scans exactly 6 stored cars.
// - CGarages::FindMaxNumStoredCarsForGarage 0x428230 returns 1/2/3 for the three vanilla hideouts.
//
// This file deliberately stays self-contained: no plugin-sdk headers are edited.

#include "plugin.h"
#include "CGarage.h"
#include "CGarages.h"
#include "CStoredCar.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {

// Additional garage type enum values for extra savehouse locations.  The vanilla
// plugin-sdk eGarageType enum ends at GARAGE_TYPE_SALVATORES = 21, so these start
// at 22 and are meant to be passed to CGarages::AddOne/ChangeGarageType by mods.
enum eGta3SaveGarageType : unsigned char {
    SAVE_GARAGE_PORTLAND_HIDEOUT  = GARAGE_TYPE_PORTLAND_HIDEOUT,
    SAVE_GARAGE_STAUNTON_HIDEOUT  = GARAGE_TYPE_STAUNTON_HIDEOUT,
    SAVE_GARAGE_SHORESIDE_HIDEOUT = GARAGE_TYPE_SHORESIDE_HIDEOUT,

    GARAGE_TYPE_EXTRA_HIDEOUT_1  = 22,
    GARAGE_TYPE_EXTRA_HIDEOUT_2  = 23,
    GARAGE_TYPE_EXTRA_HIDEOUT_3  = 24,
    GARAGE_TYPE_EXTRA_HIDEOUT_4  = 25,
    GARAGE_TYPE_EXTRA_HIDEOUT_5  = 26,
    GARAGE_TYPE_EXTRA_HIDEOUT_6  = 27,
    GARAGE_TYPE_EXTRA_HIDEOUT_7  = 28,
    GARAGE_TYPE_EXTRA_HIDEOUT_8  = 29,
    GARAGE_TYPE_EXTRA_HIDEOUT_9  = 30,

    GARAGE_TYPE_FIRST_EXTRA_HIDEOUT = GARAGE_TYPE_EXTRA_HIDEOUT_1,
    GARAGE_TYPE_LAST_EXTRA_HIDEOUT  = GARAGE_TYPE_EXTRA_HIDEOUT_9
};

// Optional ID aliases for projects that add the extra garages after the vanilla
// script-created garage list.  These are not written into CGarage; they are helper
// constants for code that wants stable names for the additional locations.
enum eGta3ExtraSaveGarageID : short {
    GARAGE_ID_EXTRA_HIDEOUT_1 = GARAGE_ID_DONALD_LOVES_STASH + 1,
    GARAGE_ID_EXTRA_HIDEOUT_2,
    GARAGE_ID_EXTRA_HIDEOUT_3,
    GARAGE_ID_EXTRA_HIDEOUT_4,
    GARAGE_ID_EXTRA_HIDEOUT_5,
    GARAGE_ID_EXTRA_HIDEOUT_6,
    GARAGE_ID_EXTRA_HIDEOUT_7,
    GARAGE_ID_EXTRA_HIDEOUT_8,
    GARAGE_ID_EXTRA_HIDEOUT_9
};

enum eGta3SaveGarageCapacity : int {
    VANILLA_PORTLAND_CAPACITY  = 1,
    VANILLA_STAUNTON_CAPACITY  = 2,
    VANILLA_SHORESIDE_CAPACITY = 3,

    QUAD_PORTLAND_CAPACITY  = VANILLA_PORTLAND_CAPACITY * 4,
    QUAD_STAUNTON_CAPACITY  = VANILLA_STAUNTON_CAPACITY * 4,
    QUAD_SHORESIDE_CAPACITY = VANILLA_SHORESIDE_CAPACITY * 4,
    EXTRA_HIDEOUT_CAPACITY  = QUAD_SHORESIDE_CAPACITY,

    PHYSICAL_SLOTS_PER_SAFEHOUSE = 24,
    VANILLA_SAFEHOUSE_COUNT = 3,
    EXTRA_SAFEHOUSE_COUNT = 9,
    TOTAL_SAFEHOUSE_COUNT = VANILLA_SAFEHOUSE_COUNT + EXTRA_SAFEHOUSE_COUNT,
    TOTAL_PHYSICAL_SLOTS = PHYSICAL_SLOTS_PER_SAFEHOUSE * TOTAL_SAFEHOUSE_COUNT
};

constexpr std::uintptr_t kOldSafehouse1 = 0x6FA210;
constexpr std::uintptr_t kOldSafehouse2 = 0x6FA300;
constexpr std::uintptr_t kOldSafehouse3 = 0x6FA3F0;
constexpr std::uintptr_t kOldSafehouseStride = 0xF0; // 6 * sizeof(CStoredCar)

constexpr std::uint32_t kVanillaGarageSaveSize = 0x156C;
constexpr std::uint32_t kVanillaStoredCarsOffset = 0x28;
constexpr std::uint32_t kVanillaGaragesOffset = kVanillaStoredCarsOffset + (6 * VANILLA_SAFEHOUSE_COUNT * sizeof(CStoredCar));
constexpr std::uint32_t kBaseStoredCarsBytes = PHYSICAL_SLOTS_PER_SAFEHOUSE * VANILLA_SAFEHOUSE_COUNT * sizeof(CStoredCar);
constexpr std::uint32_t kTotalStoredCarsBytes = PHYSICAL_SLOTS_PER_SAFEHOUSE * TOTAL_SAFEHOUSE_COUNT * sizeof(CStoredCar);
constexpr std::uint32_t kBaseGaragesOffset = kVanillaStoredCarsOffset + kBaseStoredCarsBytes;
constexpr std::uint32_t kFinalGaragesOffset = kVanillaStoredCarsOffset + kTotalStoredCarsBytes;
constexpr std::uint32_t kGarageAndTailSize = kVanillaGarageSaveSize - kVanillaGaragesOffset;
constexpr std::uint32_t kBaseGarageSaveSize = kBaseGaragesOffset + kGarageAndTailSize;
constexpr std::uint32_t kFinalGarageSaveSize = kFinalGaragesOffset + kGarageAndTailSize;

std::array<CStoredCar, TOTAL_PHYSICAL_SLOTS> gStoredCars{};
std::array<int, TOTAL_SAFEHOUSE_COUNT> gGarageCapacities{
    QUAD_PORTLAND_CAPACITY,
    QUAD_STAUNTON_CAPACITY,
    QUAD_SHORESIDE_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY,
    EXTRA_HIDEOUT_CAPACITY
};
CGarage* gCurrentGarage = nullptr;
int gCurrentGarageStorageIndex = -1;
int gCurrentGarageCapacity = 0;

constexpr char kIniPath[] = ".\\GTA3GarageExtender.ini";
constexpr char kIniSection[] = "GarageCapacity";
constexpr const char* kIniCapacityKeys[TOTAL_SAFEHOUSE_COUNT] = {
    "PortlandHideout",
    "StauntonHideout",
    "ShoresideHideout",
    "ExtraHideout1",
    "ExtraHideout2",
    "ExtraHideout3",
    "ExtraHideout4",
    "ExtraHideout5",
    "ExtraHideout6",
    "ExtraHideout7",
    "ExtraHideout8",
    "ExtraHideout9"
};

int SaveGarageIndexFromType(unsigned char type) {
    switch (type) {
    case SAVE_GARAGE_PORTLAND_HIDEOUT:
        return 0;
    case SAVE_GARAGE_STAUNTON_HIDEOUT:
        return 1;
    case SAVE_GARAGE_SHORESIDE_HIDEOUT:
        return 2;
    default:
        if (type >= GARAGE_TYPE_FIRST_EXTRA_HIDEOUT && type <= GARAGE_TYPE_LAST_EXTRA_HIDEOUT) {
            return VANILLA_SAFEHOUSE_COUNT + (type - GARAGE_TYPE_FIRST_EXTRA_HIDEOUT);
        }
        return -1;
    }
}

bool IsSaveGarageType(unsigned char type) {
    return SaveGarageIndexFromType(type) >= 0;
}

bool IsExtraSaveGarageType(unsigned char type) {
    return type >= GARAGE_TYPE_FIRST_EXTRA_HIDEOUT && type <= GARAGE_TYPE_LAST_EXTRA_HIDEOUT;
}

CStoredCar* CarsForSaveGarageIndex(int index) {
    if (index < 0 || index >= TOTAL_SAFEHOUSE_COUNT) {
        return nullptr;
    }
    return gStoredCars.data() + (index * PHYSICAL_SLOTS_PER_SAFEHOUSE);
}

int ClampCapacity(int capacity) {
    if (capacity < 0) {
        return 0;
    }
    if (capacity > PHYSICAL_SLOTS_PER_SAFEHOUSE) {
        return PHYSICAL_SLOTS_PER_SAFEHOUSE;
    }
    return capacity;
}

int CapacityForSaveGarageIndex(int index) {
    if (index < 0 || index >= TOTAL_SAFEHOUSE_COUNT) {
        return 0;
    }
    return ClampCapacity(gGarageCapacities[index]);
}

bool IniExists() {
    std::ifstream file(kIniPath);
    return file.good();
}

void CreateDefaultIniIfMissing() {
    if (IniExists()) {
        return;
    }

    std::ofstream file(kIniPath);
    if (!file.is_open()) {
        return;
    }

    file << "; GTA III Garage Extender configuration\n";
    file << "; Created automatically on first launch. Existing files are not overwritten.\n";
    file << "; Values are clamped from 0 to " << PHYSICAL_SLOTS_PER_SAFEHOUSE << " because each savehouse bank reserves " << PHYSICAL_SLOTS_PER_SAFEHOUSE << " physical slots.\n";
    file << "; Set a value to 0 to disable storing cars for that garage type.\n\n";
    file << "[" << kIniSection << "]\n";
    for (int i = 0; i < TOTAL_SAFEHOUSE_COUNT; ++i) {
        file << kIniCapacityKeys[i] << "=" << gGarageCapacities[i] << "\n";
    }
}

void LoadCapacityConfig() {
    CreateDefaultIniIfMissing();

    for (int i = 0; i < TOTAL_SAFEHOUSE_COUNT; ++i) {
        const auto configured = GetPrivateProfileIntA(kIniSection, kIniCapacityKeys[i], gGarageCapacities[i], kIniPath);
        gGarageCapacities[i] = ClampCapacity(static_cast<int>(configured));
    }
}

int CapacityForGarageType(unsigned char type) {
    return CapacityForSaveGarageIndex(SaveGarageIndexFromType(type));
}

CStoredCar* StorageForGarageType(unsigned char type) {
    return CarsForSaveGarageIndex(SaveGarageIndexFromType(type));
}


int StorageIndexForGarage(CGarage* garage) {
    if (garage == gCurrentGarage && gCurrentGarageStorageIndex >= 0) {
        return gCurrentGarageStorageIndex;
    }
    return garage != nullptr ? SaveGarageIndexFromType(static_cast<unsigned char>(garage->m_nType)) : -1;
}

int CapacityForGarage(CGarage* garage) {
    if (garage == gCurrentGarage && gCurrentGarageCapacity > 0) {
        return gCurrentGarageCapacity;
    }
    return CapacityForSaveGarageIndex(StorageIndexForGarage(garage));
}

bool IsPointInsideGarage(CGarage& garage, CVector const& point) {
    return point.x > garage.m_fLeftCoord && point.x < garage.m_fRightCoord
        && point.y > garage.m_fFrontCoord && point.y < garage.m_fBackCoord
        && point.z > garage.m_fDownCoord && point.z < garage.m_fUpCoord;
}

void ClearStoredCars() {
    std::memset(gStoredCars.data(), 0, sizeof(gStoredCars));
}

void PatchUInt(std::uintptr_t address, std::uintptr_t value) {
    plugin::patch::SetUInt(address, value, true);
}

void PatchByte(std::uintptr_t address, unsigned char value) {
    plugin::patch::SetUChar(address, value, true);
}

void PatchSafehouseAddressImmediates(std::uintptr_t begin, std::uintptr_t end) {
    for (std::uintptr_t address = begin; address + sizeof(std::uint32_t) <= end; ++address) {
        const auto value = plugin::patch::GetUInt(address, true);
        std::uintptr_t replacement = 0;

        if (value >= kOldSafehouse1 && value < kOldSafehouse1 + kOldSafehouseStride) {
            replacement = reinterpret_cast<std::uintptr_t>(CarsForSaveGarageIndex(0)) + (value - kOldSafehouse1);
        } else if (value >= kOldSafehouse2 && value < kOldSafehouse2 + kOldSafehouseStride) {
            replacement = reinterpret_cast<std::uintptr_t>(CarsForSaveGarageIndex(1)) + (value - kOldSafehouse2);
        } else if (value >= kOldSafehouse3 && value < kOldSafehouse3 + kOldSafehouseStride) {
            replacement = reinterpret_cast<std::uintptr_t>(CarsForSaveGarageIndex(2)) + (value - kOldSafehouse3);
        }

        if (replacement != 0) {
            PatchUInt(address, replacement);
            address += sizeof(std::uint32_t) - 1;
        }
    }
}

void __cdecl InitGaragesHook() {
    plugin::Call<0x421C60>();
    ClearStoredCars();
}

void __fastcall GarageUpdateHook(CGarage* garage, void* unusedEdx) {
    (void)unusedEdx;

    const auto originalType = static_cast<unsigned char>(garage->m_nType);
    if (!IsExtraSaveGarageType(originalType)) {
        plugin::CallMethod<0x4222D0, CGarage*>(garage);
        return;
    }

    gCurrentGarage = garage;
    gCurrentGarageStorageIndex = SaveGarageIndexFromType(originalType);
    gCurrentGarageCapacity = CapacityForSaveGarageIndex(gCurrentGarageStorageIndex);

    garage->m_nType = GARAGE_TYPE_SHORESIDE_HIDEOUT;
    plugin::CallMethod<0x4222D0, CGarage*>(garage);
    garage->m_nType = static_cast<eGarageType>(originalType);

    gCurrentGarage = nullptr;
    gCurrentGarageStorageIndex = -1;
    gCurrentGarageCapacity = 0;
}

int __cdecl FindMaxNumStoredCarsForGarageHook(unsigned char type) {
    if (gCurrentGarage != nullptr) {
        return gCurrentGarageCapacity;
    }
    return CapacityForGarageType(type);
}

int __cdecl CountCarsInHideoutGarageHook(unsigned char type) {
    CStoredCar* cars = gCurrentGarage != nullptr ? CarsForSaveGarageIndex(gCurrentGarageStorageIndex) : StorageForGarageType(type);
    const auto capacity = gCurrentGarage != nullptr ? gCurrentGarageCapacity : CapacityForGarageType(type);
    if (cars == nullptr || capacity <= 0) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < capacity; ++i) {
        if (cars[i].m_nModelIndex != 0) {
            ++count;
        }
    }
    return count;
}

void __fastcall StoreAndRemoveCarsForThisHideOutHook(CGarage* garage, void* unusedEdx, CStoredCar* cars, int count) {
    (void)unusedEdx;

    const auto storageIndex = StorageIndexForGarage(garage);
    CStoredCar* redirectedCars = CarsForSaveGarageIndex(storageIndex);
    const auto redirectedCount = CapacityForGarage(garage);
    if (redirectedCars != nullptr && redirectedCount > 0) {
        cars = redirectedCars;
        count = redirectedCount;
    }

    if (cars != nullptr && count > 0) {
        std::memset(cars, 0, sizeof(CStoredCar) * count);
    }

    // The original routine uses the caller-provided count for storing, but only
    // cleared six slots internally.  Pre-clearing here keeps stale expanded slots
    // from surviving after a later save with fewer cars.
    plugin::CallMethod<0x427840, CGarage*, CStoredCar*, int>(garage, cars, count);
}

bool __fastcall RestoreCarsForThisHideOutHook(CGarage* garage, void* unusedEdx, CStoredCar* cars) {
    (void)unusedEdx;

    CStoredCar* redirectedCars = CarsForSaveGarageIndex(StorageIndexForGarage(garage));
    if (redirectedCars != nullptr) {
        cars = redirectedCars;
    }
    return plugin::CallMethodAndReturn<bool, 0x427A40, CGarage*, CStoredCar*>(garage, cars);
}

bool __cdecl IsPointWithinHideOutGarageHook(CVector& point) {
    const auto count = CGarages::NumGarages < 32 ? CGarages::NumGarages : 32;
    for (unsigned int i = 0; i < count; ++i) {
        CGarage& garage = CGarages::aGarages[i];
        if (IsSaveGarageType(static_cast<unsigned char>(garage.m_nType)) && IsPointInsideGarage(garage, point)) {
            return true;
        }
    }
    return false;
}

void CloseExtraHideOutGaragesBeforeSave() {
    const auto count = CGarages::NumGarages < 32 ? CGarages::NumGarages : 32;
    for (unsigned int i = 0; i < count; ++i) {
        CGarage& garage = CGarages::aGarages[i];
        if (!IsExtraSaveGarageType(static_cast<unsigned char>(garage.m_nType)) || garage.m_nState == GARAGE_STATE_CLOSED) {
            continue;
        }

        garage.m_nState = GARAGE_STATE_CLOSED;
        StoreAndRemoveCarsForThisHideOutHook(&garage, nullptr, CarsForSaveGarageIndex(SaveGarageIndexFromType(static_cast<unsigned char>(garage.m_nType))), CapacityForGarage(&garage));
        garage.RemoveCarsBlockingDoorNotInside();
        garage.m_fDoorCurrentAngle = 0.0f;
        garage.UpdateDoorsHeight();
    }
}

void __cdecl SaveGaragesHook(unsigned char* buffer, unsigned int* size) {
    if (buffer == nullptr || size == nullptr) {
        return;
    }

    CloseExtraHideOutGaragesBeforeSave();

    alignas(CStoredCar) std::array<unsigned char, kBaseGarageSaveSize> base{};
    unsigned int baseSize = 0;
    plugin::Call<0x4284E0, unsigned char*, unsigned int*>(base.data(), &baseSize);

    std::memcpy(buffer, base.data(), kBaseGaragesOffset);
    std::memcpy(buffer + kBaseGaragesOffset, CarsForSaveGarageIndex(VANILLA_SAFEHOUSE_COUNT),
        EXTRA_SAFEHOUSE_COUNT * PHYSICAL_SLOTS_PER_SAFEHOUSE * sizeof(CStoredCar));
    std::memcpy(buffer + kFinalGaragesOffset, base.data() + kBaseGaragesOffset, kGarageAndTailSize);
    *size = kFinalGarageSaveSize;
}

void LoadBaseGarageBlock(unsigned char* base) {
    plugin::Call<0x428940, unsigned char*, unsigned int>(base, kBaseGarageSaveSize);
}

void __cdecl LoadGaragesHook(unsigned char* buffer, unsigned int size) {
    if (buffer == nullptr) {
        return;
    }

    if (size >= kFinalGarageSaveSize) {
        alignas(CStoredCar) std::array<unsigned char, kBaseGarageSaveSize> base{};
        std::memcpy(base.data(), buffer, kBaseGaragesOffset);
        std::memcpy(base.data() + kBaseGaragesOffset, buffer + kFinalGaragesOffset, kGarageAndTailSize);
        LoadBaseGarageBlock(base.data());
        std::memcpy(CarsForSaveGarageIndex(VANILLA_SAFEHOUSE_COUNT), buffer + kBaseGaragesOffset,
            EXTRA_SAFEHOUSE_COUNT * PHYSICAL_SLOTS_PER_SAFEHOUSE * sizeof(CStoredCar));
        return;
    }

    if (size >= kBaseGarageSaveSize) {
        LoadBaseGarageBlock(buffer);
        std::memset(CarsForSaveGarageIndex(VANILLA_SAFEHOUSE_COUNT), 0,
            EXTRA_SAFEHOUSE_COUNT * PHYSICAL_SLOTS_PER_SAFEHOUSE * sizeof(CStoredCar));
        return;
    }

    // Backward-compatible path for vanilla saves.  Expand the old block in-place
    // into a temporary base block: keep the first six interleaved stored-car
    // records, zero the expanded vanilla banks, and move the garage array/tail.
    alignas(CStoredCar) std::array<unsigned char, kBaseGarageSaveSize> base{};
    const auto bytesToCopy = size < kVanillaGarageSaveSize ? size : kVanillaGarageSaveSize;

    std::memcpy(base.data(), buffer, bytesToCopy);
    std::memset(base.data() + kVanillaStoredCarsOffset, 0, kBaseStoredCarsBytes);

    if (size > kVanillaStoredCarsOffset) {
        const auto vanillaStoredBytes = kVanillaGaragesOffset - kVanillaStoredCarsOffset;
        const auto availableStoredBytes = size - kVanillaStoredCarsOffset;
        const auto storedBytesToCopy = availableStoredBytes < vanillaStoredBytes ? availableStoredBytes : vanillaStoredBytes;
        std::memcpy(base.data() + kVanillaStoredCarsOffset, buffer + kVanillaStoredCarsOffset, storedBytesToCopy);
    }

    if (size > kVanillaGaragesOffset) {
        const auto availableGarageBytes = size - kVanillaGaragesOffset;
        const auto garageBytesToCopy = availableGarageBytes < kGarageAndTailSize ? availableGarageBytes : kGarageAndTailSize;
        std::memcpy(base.data() + kBaseGaragesOffset, buffer + kVanillaGaragesOffset, garageBytesToCopy);
    }

    LoadBaseGarageBlock(base.data());
    std::memset(CarsForSaveGarageIndex(VANILLA_SAFEHOUSE_COUNT), 0,
        EXTRA_SAFEHOUSE_COUNT * PHYSICAL_SLOTS_PER_SAFEHOUSE * sizeof(CStoredCar));
}

void InstallPatches() {
    LoadCapacityConfig();
    ClearStoredCars();

    // Any absolute reference to the vanilla safehouse arrays inside the garage code
    // is retargeted to our expanded, contiguous storage banks for the three vanilla
    // hideouts.  The nine extra hideouts are handled by the redirected hooks below.
    PatchSafehouseAddressImmediates(0x421C60, 0x428D84);

    // CGarages::Init call sites; clear the expanded banks after the original init.
    plugin::patch::RedirectCall(0x48C2CB, InitGaragesHook, true);
    plugin::patch::RedirectCall(0x48C5B2, InitGaragesHook, true);
    plugin::patch::RedirectCall(0x48C689, InitGaragesHook, true);

    // Save/Load layout growth.  The original Save/Load routines still handle the
    // first three banks; SaveGaragesHook/LoadGaragesHook splice in the nine new banks.
    PatchUInt(0x4284F6, kBaseGarageSaveSize); // CGarages::Save returned base block size.
    PatchByte(0x428761, PHYSICAL_SLOTS_PER_SAFEHOUSE); // CGarages::Save vanilla-bank loop count.
    PatchByte(0x428BA8, PHYSICAL_SLOTS_PER_SAFEHOUSE); // CGarages::Load vanilla-bank loop count.
    plugin::patch::RedirectCall(0x58FD28, SaveGaragesHook, true); // GenericSave -> CGarages::Save.
    plugin::patch::RedirectCall(0x590EDE, LoadGaragesHook, true); // GenericLoad -> CGarages::Load.

    // Runtime storage/restore/count limits.
    plugin::patch::RedirectCall(0x421E9F, GarageUpdateHook, true); // CGarages::Update -> CGarage::Update.
    plugin::patch::RedirectJump(0x428230, FindMaxNumStoredCarsForGarageHook, true);
    plugin::patch::RedirectJump(0x4281E0, CountCarsInHideoutGarageHook, true);
    plugin::patch::RedirectJump(0x428260, IsPointWithinHideOutGarageHook, true);
    PatchByte(0x427A7D, PHYSICAL_SLOTS_PER_SAFEHOUSE); // restore pass 1 loop.
    PatchByte(0x427A9C, PHYSICAL_SLOTS_PER_SAFEHOUSE); // restore pass 2 loop.

    // Update() store/restore call capacities.  These are configurable in the INI.
    PatchByte(0x424A36, static_cast<unsigned char>(CapacityForSaveGarageIndex(0)));
    PatchByte(0x424A3F, static_cast<unsigned char>(CapacityForSaveGarageIndex(1)));
    PatchByte(0x424A48, static_cast<unsigned char>(CapacityForSaveGarageIndex(2)));
    plugin::patch::RedirectCall(0x424A50, StoreAndRemoveCarsForThisHideOutHook, true);
    plugin::patch::RedirectCall(0x424BED, RestoreCarsForThisHideOutHook, true);

    // CloseHideOutGaragesBeforeSave() capacities for vanilla hideouts.
    PatchByte(0x428188, static_cast<unsigned char>(CapacityForSaveGarageIndex(0)));
    PatchByte(0x428191, static_cast<unsigned char>(CapacityForSaveGarageIndex(1)));
    PatchByte(0x42819A, static_cast<unsigned char>(CapacityForSaveGarageIndex(2)));
    plugin::patch::RedirectCall(0x4281A2, StoreAndRemoveCarsForThisHideOutHook, true);
}

class QuadrupleSaveGarageLimitPlugin {
public:
    QuadrupleSaveGarageLimitPlugin() {
        InstallPatches();
    }
} gQuadrupleSaveGarageLimitPlugin;

} // namespace
