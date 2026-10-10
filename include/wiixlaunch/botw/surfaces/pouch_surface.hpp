#pragma once

// botw.pouch v1 - the inventory.
//
// ---------------------------------------------------------------------------
// ENUMERATION IS THE HARD PART, and it decided the shape of this surface.
//
// The module walks the pouch with ForEachOfType(slot, callback), handing each
// entry to a lambda as a struct. Neither half of that can cross: a template
// takes a callable the host cannot receive from a compiled binary, and Entry is
// a struct with fifteen fields whose layout a mod must never depend on.
//
// So enumeration here is COUNT plus INDEXED ACCESS, the same shape as the flag
// store in botw.gamedata. It costs a walk per item where the callback form
// walked once for all of them - a real cost, paid deliberately, because the
// alternative is a mod that reads a struct field at the wrong offset and
// reports someone else's durability as a cook effect.
//
// A mod enumerating the whole pouch every frame will feel that. One doing it
// when a menu opens will not, and that is the honest use for it.
//
// ---------------------------------------------------------------------------
// THE MODIFIER WORDS MEAN TWO DIFFERENT THINGS, which is the trap this API is
// shaped around. On a weapon (type 0-3) they are the modifier flags and value;
// on food (type 8) the same words are cook data. Reading one as the other
// produces a plausible-looking number rather than an error, so ItemAt reports
// `isFood` and the two readings are separate calls. Nothing here will hand back
// a cook effect for a sword.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/botw/game/pouch.hpp>

#include <cstdint>

namespace WiiXLaunch::BotW::Surfaces::PouchSurface {

constexpr const char* kName = "botw.pouch";
constexpr uint16_t kVersionMajor = 1;
// 1.1 appends TickEquipRefresh. Appending bumps the MINOR, so every mod built
// against v1.0 still resolves.
constexpr uint16_t kVersionMinor = 1;

namespace impl {

inline uint32_t CopyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!src) return 0;
    uint32_t n = 0;
    while (src[n] && n + 1 < cap) { out[n] = src[n]; ++n; }
    out[n] = '\0';
    return n;
}

// The nth entry of a type, copied out of the walk. Returns false when the
// index is past the end - which is how a caller stops, rather than by trusting
// a count it read a frame ago.
inline bool EntryAt(Pouch::Slot slot, int index, Pouch::Entry& out) {
    bool found = false;
    int seen = 0;
    Pouch::ForEachOfType(slot, [&](const Pouch::Entry& e) {
        if (seen++ != index) return true;
        out = e;
        found = true;
        return false;               // stop; the walk is done
    });
    return found;
}

// --- capability ------------------------------------------------------------

extern "C" inline uint32_t PSupportsPouch() {
    return Pouch::SupportsEquippedValue ? 1u : 0u;
}

extern "C" inline int32_t PSlotCount(int32_t slot) {
    return static_cast<int32_t>(Pouch::CountOfType(static_cast<Pouch::Slot>(slot)));
}

// --- one entry, by index ---------------------------------------------------
//
// Deliberately several small calls rather than one that fills a caller-provided
// struct. A struct would have to be laid out identically on both sides forever;
// these can be appended to without ever changing what an existing mod reads.

extern "C" inline uint32_t PItemName(int32_t slot, int32_t index, char* out, uint32_t cap) {
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) {
        if (out && cap) out[0] = '\0';
        return 0;
    }
    return CopyOut(e.name, out, cap);
}

// Count for materials, durability for weapons - the module's own convention,
// and HasMeaningfulValue says which slots it means anything for at all.
extern "C" inline uint32_t PItemValue(int32_t slot, int32_t index, int32_t* out) {
    if (!out) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    *out = static_cast<int32_t>(e.value);
    return 1;
}

extern "C" inline uint32_t PItemPouchIndex(int32_t slot, int32_t index, int32_t* out) {
    if (!out) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    *out = static_cast<int32_t>(e.index);
    return 1;
}

extern "C" inline uint32_t PItemIsEquipped(int32_t slot, int32_t index, uint32_t* out) {
    if (!out) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    *out = e.equipped ? 1u : 0u;
    return 1;
}

extern "C" inline uint32_t PItemIsFood(int32_t slot, int32_t index, uint32_t* out) {
    if (!out) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    *out = e.isFood ? 1u : 0u;
    return 1;
}

// Weapons only. Returns 0 for anything else rather than the food reading of the
// same words - see the header comment.
extern "C" inline uint32_t PItemModifier(int32_t slot, int32_t index,
                                         uint32_t* flags, int32_t* value) {
    if (!flags || !value) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    if (e.isFood) return 0;
    *flags = e.modifierFlags;
    *value = e.modifierValue;
    return 1;
}

// Food only, for the same reason in the other direction.
extern "C" inline uint32_t PItemCookData(int32_t slot, int32_t index,
                                         int32_t* health, int32_t* duration,
                                         int32_t* sellPrice,
                                         float* effectId, float* effectLevel) {
    if (!health || !duration || !sellPrice || !effectId || !effectLevel) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    if (!e.isFood) return 0;
    *health = e.cookHealth;
    *duration = e.cookDuration;
    *sellPrice = e.cookSellPrice;
    *effectId = e.cookEffectId;
    *effectLevel = e.cookEffectLevel;
    return 1;
}

// --- equipped --------------------------------------------------------------

extern "C" inline uint32_t PGetEquippedName(int32_t slot, char* out, uint32_t cap) {
    return CopyOut(Pouch::GetEquippedName(static_cast<Pouch::Slot>(slot)), out, cap);
}

extern "C" inline uint32_t PGetEquippedValue(int32_t slot, int32_t* out) {
    if (!out) return 0;
    int v = 0;
    if (!Pouch::GetEquippedValue(static_cast<Pouch::Slot>(slot), v)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

extern "C" inline uint32_t PSetEquippedValue(int32_t slot, int32_t value) {
    return Pouch::SetEquippedValue(static_cast<Pouch::Slot>(slot), static_cast<int>(value))
               ? 1u : 0u;
}

extern "C" inline uint32_t PHasMeaningfulValue(int32_t slot) {
    return Pouch::HasMeaningfulValue(static_cast<Pouch::Slot>(slot)) ? 1u : 0u;
}

extern "C" inline uint32_t PIsWeaponSlot(int32_t slot) {
    return Pouch::IsWeaponSlot(static_cast<Pouch::Slot>(slot)) ? 1u : 0u;
}

// --- changing the pouch ----------------------------------------------------

extern "C" inline uint32_t PAddItem(const char* name, int32_t value) {
    return name && Pouch::AddItem(name, static_cast<int>(value)) ? 1u : 0u;
}

// Returns how many were actually removed, which may be fewer than asked for.
// Not a bool: "you asked for 5 and got 2" is a different outcome from both
// success and failure, and a bool would have hidden it.
extern "C" inline int32_t PRemoveItem(const char* name, int32_t count) {
    if (!name) return 0;
    return static_cast<int32_t>(Pouch::RemoveItem(name, static_cast<int>(count)));
}

extern "C" inline uint32_t PEquipItem(const char* name) {
    return name && Pouch::EquipItem(name) ? 1u : 0u;
}

extern "C" inline int32_t PGetTypeForName(const char* name) {
    return name ? static_cast<int32_t>(Pouch::GetTypeForName(name)) : -1;
}

// --- writing an item's own data -------------------------------------------
//
// The module's writers take a POINTER to the item, which they get from
// FindItem. A mod cannot hold that pointer safely - it is a pointer into the
// pouch that the next inventory change may invalidate - so everything here is
// addressed the way reads already are: by (slot, index), resolved on the spot.
// One walk per call, the same cost the reads pay, and no way to hold a stale
// item across a frame.

extern "C" inline uint32_t PSetCookData(int32_t slot, int32_t index,
                                        int32_t health, int32_t duration,
                                        int32_t sellPrice,
                                        float effectId, float effectLevel) {
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    if (!e.isFood) return 0;   // these words are the modifier on a weapon

    Pouch::CookData d;
    d.setHealth = true;     d.health = health;
    d.setDuration = true;   d.duration = duration;
    d.setSellPrice = true;  d.sellPrice = sellPrice;
    d.setEffectId = true;   d.effectId = effectId;
    d.setEffectLevel = true; d.effectLevel = effectLevel;
    return Pouch::SetCookData(e.node, d) ? 1u : 0u;
}

extern "C" inline uint32_t PSetItemModifier(int32_t slot, int32_t index,
                                            uint32_t flags, int32_t value) {
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    if (e.isFood) return 0;    // these words are cook data on food
    return Pouch::SetModifierAt(e.node, flags, value) ? 1u : 0u;
}

// The by-name form the module offers, kept because it is what a mod usually
// wants: "put a modifier on my Royal Broadsword" without first finding which
// index it is.
extern "C" inline uint32_t PSetModifierByName(const char* name, uint32_t flags,
                                              int32_t value) {
    return name && Pouch::SetModifier(name, flags, value) ? 1u : 0u;
}

// Does an item of this name exist, and at which index within its slot? -1 when
// it is not there. The index is what every other call here takes.
extern "C" inline int32_t PFindItemIndex(int32_t slot, const char* name) {
    if (!name) return -1;
    int found = -1;
    int seen = 0;
    Pouch::ForEachOfType(static_cast<Pouch::Slot>(slot), [&](const Pouch::Entry& e) {
        if (e.name) {
            const char* a = e.name;
            const char* b = name;
            while (*a && *a == *b) { ++a; ++b; }
            if (*a == *b) { found = seen; return false; }
        }
        ++seen;
        return true;
    });
    return static_cast<int32_t>(found);
}

extern "C" inline uint32_t PItemStacks(int32_t slot, int32_t index, uint32_t* out) {
    if (!out) return 0;
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) return 0;
    *out = Pouch::ItemStacks(e.node) ? 1u : 0u;
    return 1;
}

// One of a cooked meal's ingredient names, by slot 0..4. Empty when that
// ingredient slot is unused, which is ordinary rather than an error.
extern "C" inline uint32_t PCookIngredient(int32_t slot, int32_t index,
                                           int32_t ingredientSlot,
                                           char* out, uint32_t cap) {
    Pouch::Entry e{};
    if (!EntryAt(static_cast<Pouch::Slot>(slot), static_cast<int>(index), e)) {
        if (out && cap) out[0] = '\0';
        return 0;
    }
    if (!e.isFood) { if (out && cap) out[0] = '\0'; return 0; }
    return CopyOut(Pouch::CookIngredient(e.node, static_cast<int>(ingredientSlot)), out, cap);
}

extern "C" inline uint32_t PIsEquippableSlot(int32_t slot) {
    return Pouch::IsEquippableSlot(static_cast<Pouch::Slot>(slot)) ? 1u : 0u;
}

// --- names for the numbers -------------------------------------------------

extern "C" inline uint32_t PCookEffectName(int32_t id, char* out, uint32_t cap) {
    return CopyOut(Pouch::CookEffectName(static_cast<int>(id)), out, cap);
}

extern "C" inline int32_t PCookEffectFromName(const char* name) {
    return name ? static_cast<int32_t>(Pouch::CookEffectFromName(name))
                : static_cast<int32_t>(Pouch::kCookEffectNone);
}

extern "C" inline uint32_t PModifierFromName(const char* name) {
    return name ? Pouch::ModifierFromName(name) : 0u;
}

// An equip does not take effect the moment it is asked for: the game re-reads
// the pouch a frame or two later, and the module holds the request until then.
// SOMETHING HAS TO PUMP IT. The coverage gate called this "internal, driven by
// the module's own tick" - the module has no tick of its own, and the API
// server was what drove it. A mod that equips or repairs and never calls this
// gets a write that quietly never lands.
extern "C" inline void PTickEquipRefresh() {
#if !WIIXL_SWITCH
    Pouch::TickEquipRefresh();
#endif
}

// --- the table -------------------------------------------------------------
inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("SupportsPouch",      &PSupportsPouch),
    WIIXL_SURFACE_SYMBOL("SlotCount",          &PSlotCount),

    WIIXL_SURFACE_SYMBOL("ItemName",           &PItemName),
    WIIXL_SURFACE_SYMBOL("ItemValue",          &PItemValue),
    WIIXL_SURFACE_SYMBOL("ItemPouchIndex",     &PItemPouchIndex),
    WIIXL_SURFACE_SYMBOL("ItemIsEquipped",     &PItemIsEquipped),
    WIIXL_SURFACE_SYMBOL("ItemIsFood",         &PItemIsFood),
    WIIXL_SURFACE_SYMBOL("ItemModifier",       &PItemModifier),
    WIIXL_SURFACE_SYMBOL("ItemCookData",       &PItemCookData),

    WIIXL_SURFACE_SYMBOL("GetEquippedName",    &PGetEquippedName),
    WIIXL_SURFACE_SYMBOL("GetEquippedValue",   &PGetEquippedValue),
    WIIXL_SURFACE_SYMBOL("SetEquippedValue",   &PSetEquippedValue),
    WIIXL_SURFACE_SYMBOL("HasMeaningfulValue", &PHasMeaningfulValue),
    WIIXL_SURFACE_SYMBOL("IsWeaponSlot",       &PIsWeaponSlot),

    WIIXL_SURFACE_SYMBOL("AddItem",            &PAddItem),
    WIIXL_SURFACE_SYMBOL("RemoveItem",         &PRemoveItem),
    WIIXL_SURFACE_SYMBOL("EquipItem",          &PEquipItem),
    WIIXL_SURFACE_SYMBOL("GetTypeForName",     &PGetTypeForName),

    WIIXL_SURFACE_SYMBOL("SetCookData",        &PSetCookData),
    WIIXL_SURFACE_SYMBOL("SetItemModifier",    &PSetItemModifier),
    WIIXL_SURFACE_SYMBOL("SetModifierByName",  &PSetModifierByName),
    WIIXL_SURFACE_SYMBOL("FindItemIndex",      &PFindItemIndex),
    WIIXL_SURFACE_SYMBOL("ItemStacks",         &PItemStacks),
    WIIXL_SURFACE_SYMBOL("CookIngredient",     &PCookIngredient),
    WIIXL_SURFACE_SYMBOL("IsEquippableSlot",   &PIsEquippableSlot),

    WIIXL_SURFACE_SYMBOL("CookEffectName",     &PCookEffectName),
    WIIXL_SURFACE_SYMBOL("CookEffectFromName", &PCookEffectFromName),
    WIIXL_SURFACE_SYMBOL("ModifierFromName",   &PModifierFromName),
    // v1.1. Appended, never inserted.
    WIIXL_SURFACE_SYMBOL("TickEquipRefresh",   &PTickEquipRefresh),
};

} // namespace impl

inline bool Register() {
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::BotW::Surfaces::PouchSurface
