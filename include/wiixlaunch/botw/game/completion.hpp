#pragma once

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/call.hpp>
#include <wiixlaunch/hook.hpp>
#include "gamedata.hpp"

// WiiXLaunch::BotW::Completion - the completion percentage the map shows once
// you have beaten the game.
//
// READ THIS FIRST: the percentage is not stored anywhere. There is no flag, no
// save field and no cached number to write. The map UI recomputes it from four
// counters every time it refreshes (0x02ed5608), as
//
//     percent = collected * 100 / total
//
// with, term for term out of that function:
//
//   collected = 2                                  always, once GameClear is set
//             + 1 if the DLC is installed AND Clear_FinalTrial is set
//             + how many of the four Divine Beast flags are set
//             + the HiddenKorok_Number flag
//             + the map manager's own tally at +0x50c
//
//   total     = 2
//             + 1 if the DLC is installed
//             + 4                                  the Divine Beasts
//             + HiddenKorok_Number's declared maximum (900)
//             + the map manager's own total at +0x508
//
// On a vanilla save that arithmetic lands on 1169, which is the figure the
// community has always quoted (900 Koroks + 120 shrines + 136 quests + 13
// memories = 1169, with six of those items being the two base ones and the four
// Divine Beasts that this formula counts separately). Every term here is read
// live rather than assumed, so a DLC or Master Mode save is handled by the same
// code.
//
// So this header reads and it does NOT offer a SetCompletionPercent. There is
// nothing that write could land on. The only large, save-backed, freely
// writable term is the Korok count - the Divine Beast and Trial of the Sword
// flags are worth five items between them, and the map manager's tally is a UI
// number it recomputes for itself. A "set the percentage" call would therefore
// be SetKorokCount wearing a misleading name: it would silently rewrite your
// seed total, it could not reach 100% unless the shrines and quests were
// already done, and it would land on whole-seed steps rather than the number
// asked for.
//
// So move the counter yourself and be aware of what you moved. GetCompletion
// gives you every term, and GetReachableRange tells you the band the Korok
// count can push the percentage across:
//
//   Completion::Breakdown b;
//   Completion::GetCompletion(b);
//   const int fixed = b.collected - b.koroks;
//   Completion::SetKorokCount((int)(want * b.total / 100.0f + 0.5f) - fixed);
//
// If what you want is for the NUMBER ON SCREEN to read whatever you say,
// regardless of save state, that is the second half of this header: call Init()
// once and then SetDisplayedPercent. It hooks the calculation and repaints the
// pane, so it can show 100% on a save that has earned 3% - but it changes only
// what is drawn. The save is untouched, and so is every counter above. Since
// nothing in the game ever reads the percentage back, that is the whole feature
// rather than a limitation, but it does mean the two halves of this header can
// disagree: GetCompletionPercent keeps telling you the truth.
//
// Addresses are Wii U V208 - see data/symbols-wiiu-v208.csv. Nothing here was
// RE'd on Switch, so every call is a no-op returning false there, matching the
// rest of the framework.

namespace WiiXLaunch::BotW::Completion {

static constexpr bool SupportsCompletion = !WIIXL_SWITCH;

// The flag holding the number of Korok seeds FOUND. Not to be confused with
// KorokNutsNum, which is how many you are currently carrying to spend at Hestu
// and has nothing to do with completion.
constexpr const char* kKorokFlag = "HiddenKorok_Number";

// The flag that gates the whole display: the map only draws a percentage at
// all once this is set.
constexpr const char* kGameClearFlag = "GameClear";

// The DLC's Trial of the Sword, worth one completion item when the DLC is
// installed.
constexpr const char* kFinalTrialFlag = "Clear_FinalTrial";

namespace impl {

// ksys::ui map manager singleton pointer. Its two tally words are the term the
// formula gets from the UI rather than from a flag: +0x508 is how many things
// it counts in total and +0x50c how many of them are done. Read live because
// nothing here can usefully guess what the map decided to count.
constexpr uintptr_t kMapMgrPtrWiiU = 0x104698f8;
constexpr uintptr_t kMapTotal = 0x508;
constexpr uintptr_t kMapCollected = 0x50c;

// The version/DLC manager. The UI tests `(*(u32*)(mgr + 0x78) >> 8) > 2` for
// "the DLC is installed", which is worth one extra completion item.
constexpr uintptr_t kVersionMgrPtrWiiU = 0x1046cd60;
constexpr uintptr_t kVersionField = 0x78;

// The Divine Beast predicate table the UI walks: a count and a pointer to that
// many `int(*)(bool debug)` gdt bool getters. Both written by 0x02ed5ab0, and
// on V208 the count is 4 and the entries are the four Clear_Remains* getters -
// 0x02e17874 Wind, 0x02e17844 Electric, 0x02e17864 Water, 0x02e17854 Fire.
//
// Walked rather than hard-coded so that whatever the table actually holds is
// what gets counted, which is also how the game does it.
constexpr uintptr_t kBeastCountPtrWiiU = 0x1052d3cc;
constexpr uintptr_t kBeastTablePtrWiiU = 0x1052d3d0;
constexpr int kMaxBeastEntries = 16;   // sanity bound on a runtime-written count

using BeastFlagFn = int (*)(int debug);

// ksys::gdt: the flag's declared MAXIMUM by name, which is where the 900 comes
// from. 0x0321072c resolves the name against core+0x10 and reads the flag
// object's properties; Ghidra types it void but it tail-returns the inner
// call's success, which is what the UI branches on.
//
// It takes the core hanging off the manager at +0x70c, not the +0x700 read core
// gamedata.hpp uses - so this resolves its own rather than borrowing that one.
constexpr uintptr_t kGdtMgrPtrWiiU = 0x1046d5b0;
constexpr uintptr_t kFlagCoreOffset = 0x70c;
constexpr uintptr_t kGetFlagMaxByNameWiiU = 0x0321072c;

using GetFlagMaxFn = int (*)(void* core, int* out, const void* name);

inline bool PlausiblePointer(uintptr_t addr) {
    // 0xf0000000, not the 0xa0000000 most of these headers use. That lower cap
    // is wrong for this game: gamedata.hpp already had to widen its own after a
    // gdt write core turned up at 0xa0000238 and every forced flag write was
    // being refused as an implausible pointer. Singletons allocated up there
    // read as null through the smaller bound, which is silent - the manager
    // just looks like it does not exist yet.
    return addr >= 0x10000000 && addr < 0xf0000000;
}

// The same question for a FUNCTION pointer, which needs the opposite range.
// Code lives low - .text starts at 0x02000020 and the highest address this
// framework calls into is around 0x0421xxxx - so every code pointer fails
// PlausiblePointer's data-and-heap bound. Using that one on the Divine Beast
// predicate table skipped all four entries and reported nought of four on a
// save with all four flags set.
inline bool PlausibleCode(uintptr_t addr) {
    return addr >= 0x02000000 && addr < 0x10000000 && (addr & 3) == 0;
}

// ksys::ui map completion display. Never CALLED - it writes into a layout it
// owns, so it is no use to a caller that just wants the number - but it is
// where every constant above comes from, and it is what Init() hooks.
//
// Its prologue is a plain `stwu r1,-0x68(r1); mfspr r0,LR; stmw r25,0x4c(r1)`
// with nothing position-dependent, and it has exactly one caller (0x02f5f8a4),
// so a trampoline over it is as safe as the one player.hpp already installs.
constexpr uintptr_t kUpdateCompletionDisplayWiiU = 0x02ed5608;

// The layout the pane belongs to hangs off the display object at +0x0c. Both
// of the calls below take it, not the display object itself.
constexpr uintptr_t kPaneOwner = 0x0c;
constexpr const char* kCompletionPane = "T_Comp_00";

// (owner, sead::SafeString* paneName, sead::SafeString* text). Widens the ASCII
// text to UTF-16 into a stack buffer and forwards to the real setter at
// 0x03082d88. It only ever touches text[0] (the char*) and text[1] (the vtable),
// so the plain SafeString this framework builds everywhere is the right shape -
// the game's own caller passes a sead::BufferedSafeString, but the extra fields
// go unread.
constexpr uintptr_t kSetPaneTextWiiU = 0x03082e18;

// (owner, u8 visible). Whole body is a read-modify-write of bit 0 at
// owner->[0x0c]+0x44. The game uses it to hide the pane when GameClear is
// clear, which is why showing an overridden number before the game is beaten
// needs it called again afterwards.
constexpr uintptr_t kSetPaneVisibleWiiU = 0x02ed55e0;

// (int) -> const char*, the locale's decimal separator: "." normally, "," for
// locale 0x0b. Called so an overridden number punctuates like the real one.
constexpr uintptr_t kDecimalSeparatorWiiU = 0x0308405c;

using SetPaneTextFn = void (*)(void* owner, const void* paneName, const void* text);
using SetPaneVisibleFn = void (*)(void* owner, uint8_t visible);
using DecimalSeparatorFn = const char* (*)(int mode);

// --- display override state ---------------------------------------------
inline bool& OverrideActive() { static bool v = false; return v; }
inline int& OverrideWhole() { static int v = 0; return v; }
inline int& OverrideHundredths() { static int v = 0; return v; }
inline bool& OverrideForceVisible() { static bool v = false; return v; }
inline bool& HookInstalled() { static bool v = false; return v; }

// Decimal, right-aligned into at least minDigits, appended to p. Hand-rolled
// rather than reached for from <cstdio> because this header has no business
// pulling in a formatter for two integers.
inline char* AppendInt(char* p, char* end, int value, int minDigits) {
    char digits[12];
    int n = 0;
    if (value < 0) value = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && n < 11);
    while (n < minDigits && n < 11) digits[n++] = '0';
    while (n > 0 && p < end) *p++ = digits[--n];
    return p;
}

// "<whole><sep><hh>/100", the same shape the game formats - its own format
// string is "%d%s%02d/100" with the separator from 0x0308405c.
inline const char* FormatOverride() {
    // 33 rather than 32, and aligned, because of how the Cemu build resolves
    // references: `end` below is a real relocation to an interior byte of this
    // buffer, and the packager can only emit a label for a WORD-ALIGNED target.
    // At 32 bytes end lands on buf+0x1f and the build fails outright. At 33 it
    // is buf+0x20, which with alignas(4) on the buffer is word-aligned. The
    // usable capacity is unchanged - the extra byte is never written.
    alignas(4) static char buf[33];
    char* p = buf;
    char* const end = buf + sizeof(buf) - 1;

    p = AppendInt(p, end, OverrideWhole(), 1);

    char separator = '.';
    if (auto sep = WiiXLaunch::GetTargetFunction<DecimalSeparatorFn>(
            0x0, kDecimalSeparatorWiiU)) {
        const char* text = sep(0);
        if (text && text[0]) separator = text[0];
    }
    if (p < end) *p++ = separator;

    p = AppendInt(p, end, OverrideHundredths(), 2);

    const char* tail = "/100";
    while (*tail && p < end) *p++ = *tail++;

    *p = '\0';
    return buf;
}

// Repaints the pane with the override, and optionally forces it visible.
inline void PaintOverride(void* display) {
#if !WIIXL_SWITCH
    if (!display) return;

    void* owner = *reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(display) + kPaneOwner);
    if (!owner || !PlausiblePointer(reinterpret_cast<uintptr_t>(owner))) return;

    auto setText = WiiXLaunch::GetTargetFunction<SetPaneTextFn>(0x0, kSetPaneTextWiiU);
    if (!setText) return;

    GameData::impl::SafeString pane = GameData::impl::MakeSafeString(kCompletionPane);
    GameData::impl::SafeString text = GameData::impl::MakeSafeString(FormatOverride());
    setText(owner, &pane, &text);

    if (OverrideForceVisible()) {
        auto setVisible = WiiXLaunch::GetTargetFunction<SetPaneVisibleFn>(
            0x0, kSetPaneVisibleWiiU);
        if (setVisible) setVisible(owner, 1);
    }
#else
    (void)display;
#endif
}

// Runs the game's own update first, then paints over the digits.
//
// Deliberately NOT a replacement. Letting the original run keeps every piece of
// its behaviour - the GameClear gate, the map-manager readiness check, the
// tallying, the pane visibility - and leaves this with one job. It also means
// that with no override set the cost is a single bool test.
WIIXL_HOOK_DEFINE_TRAMPOLINE(CompletionDisplayHook) {
    static void Callback(void* display) {
        Orig(display);
        if (OverrideActive()) PaintOverride(display);
    }
};

inline void* MapMgr() {
#if !WIIXL_SWITCH
    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kMapMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return nullptr;
    return reinterpret_cast<void*>(mgr);
#else
    return nullptr;
#endif
}

// True when the DLC is installed. Mirrors the UI's own test exactly.
inline bool HasDlc() {
#if !WIIXL_SWITCH
    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kVersionMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return false;
    return (*reinterpret_cast<uint32_t*>(mgr + kVersionField) >> 8) > 2;
#else
    return false;
#endif
}

// How many of the table's predicates are satisfied, and how many there are.
inline void CountBeasts(int& done, int& outOf) {
    done = 0;
    outOf = 0;
#if !WIIXL_SWITCH
    const int count = *reinterpret_cast<int32_t*>(kBeastCountPtrWiiU);
    if (count <= 0 || count > kMaxBeastEntries) return;

    uintptr_t table = *reinterpret_cast<uintptr_t*>(kBeastTablePtrWiiU);
    if (!PlausiblePointer(table)) return;

    outOf = count;
    for (int i = 0; i < count; ++i) {
        uintptr_t fn = *reinterpret_cast<uintptr_t*>(table + 4 * static_cast<uintptr_t>(i));
        if (!PlausibleCode(fn)) continue;
        if (reinterpret_cast<BeastFlagFn>(fn)(0) != 0) ++done;
    }
#endif
}

// An s32 flag's declared maximum, through the game's own property lookup.
inline bool FlagMax(const char* name, int& out) {
#if !WIIXL_SWITCH
    if (!name) return false;

    uintptr_t mgr = *reinterpret_cast<uintptr_t*>(kGdtMgrPtrWiiU);
    if (!PlausiblePointer(mgr)) return false;

    uintptr_t slot = *reinterpret_cast<uintptr_t*>(mgr + kFlagCoreOffset);
    if (!PlausiblePointer(slot)) return false;

    uintptr_t core = *reinterpret_cast<uintptr_t*>(slot);
    if (!PlausiblePointer(core)) return false;

    auto get = WiiXLaunch::GetTargetFunction<GetFlagMaxFn>(0x0, kGetFlagMaxByNameWiiU);
    if (!get) return false;

    GameData::impl::SafeString key = GameData::impl::MakeSafeString(name);
    int value = 0;
    if (get(reinterpret_cast<void*>(core), &value, &key) == 0) return false;

    out = value;
    return true;
#else
    (void)name; (void)out;
    return false;
#endif
}

} // namespace impl

// Every term the map's percentage is built from, so a caller can show the parts
// rather than just the total.
struct Breakdown {
    float percent;        // exactly what the map prints
    int collected;        // the numerator
    int total;            // the denominator

    int base;             // always-earned items counted toward collected
    int baseTotal;        // the same items in the denominator

    int divineBeasts;     // Divine Beast flags set
    int divineBeastTotal; // how many the game's table holds

    int koroks;           // HiddenKorok_Number
    int korokTotal;       // its declared maximum

    int mapCollected;     // the map manager's tally, +0x50c
    int mapTotal;         // the map manager's total, +0x508

    bool hasDlc;
    bool finalTrialDone;
    bool gameClear;       // whether the map would show the number at all
};

// Whether the game has been beaten. The map only draws the percentage when this
// is set, so a caller that mirrors the UI should check it - everything else
// here works regardless.
inline bool IsGameClear() {
#if !WIIXL_SWITCH
    bool value = false;
    if (!GameData::GetFlagBool(kGameClearFlag, value)) return false;
    return value;
#else
    return false;
#endif
}

// The whole computation. Returns false when there is no save loaded or the map
// manager is not built yet, in which case out is zeroed rather than half-filled.
inline bool GetCompletion(Breakdown& out) {
    out.percent = 0.0f;
    out.collected = 0; out.total = 0;
    out.base = 0; out.baseTotal = 0;
    out.divineBeasts = 0; out.divineBeastTotal = 0;
    out.koroks = 0; out.korokTotal = 0;
    out.mapCollected = 0; out.mapTotal = 0;
    out.hasDlc = false; out.finalTrialDone = false; out.gameClear = false;

#if !WIIXL_SWITCH
    void* map = impl::MapMgr();
    if (!map) return false;

    int koroks = 0;
    if (!GameData::GetFlagS32(kKorokFlag, koroks)) return false;

    int korokMax = 0;
    if (!impl::FlagMax(kKorokFlag, korokMax)) return false;

    bool finalTrial = false;
    GameData::GetFlagBool(kFinalTrialFlag, finalTrial);

    const bool dlc = impl::HasDlc();

    int beasts = 0, beastTotal = 0;
    impl::CountBeasts(beasts, beastTotal);

    uintptr_t base = reinterpret_cast<uintptr_t>(map);
    const int mapTotal = *reinterpret_cast<int32_t*>(base + impl::kMapTotal);
    const int mapCollected = *reinterpret_cast<int32_t*>(base + impl::kMapCollected);

    // The two base items are counted into both halves; the DLC's Trial of the
    // Sword raises the denominator whenever the DLC is present and the
    // numerator only once it is actually cleared.
    out.base = (dlc && finalTrial) ? 3 : 2;
    out.baseTotal = dlc ? 3 : 2;

    out.divineBeasts = beasts;
    out.divineBeastTotal = beastTotal;
    out.koroks = koroks;
    out.korokTotal = korokMax;
    out.mapCollected = mapCollected;
    out.mapTotal = mapTotal;
    out.hasDlc = dlc;
    out.finalTrialDone = finalTrial;
    out.gameClear = IsGameClear();

    out.collected = out.base + beasts + koroks + mapCollected;
    out.total = out.baseTotal + beastTotal + korokMax + mapTotal;

    if (out.total > 0) {
        out.percent = static_cast<float>(out.collected * 100) /
                      static_cast<float>(out.total);
    }
    return true;
#else
    return false;
#endif
}

// Just the number, 0.0 to 100.0.
inline bool GetCompletionPercent(float& out) {
#if !WIIXL_SWITCH
    Breakdown breakdown;
    if (!GetCompletion(breakdown)) return false;
    out = breakdown.percent;
    return true;
#else
    (void)out;
    return false;
#endif
}

// The two numbers the map actually prints: the whole part and the two-digit
// hundredths, split the same way the UI splits them (truncated, not rounded, on
// both halves) so this matches the screen digit for digit.
inline bool GetCompletionParts(int& whole, int& hundredths) {
#if !WIIXL_SWITCH
    float percent = 0.0f;
    if (!GetCompletionPercent(percent)) return false;

    const int w = static_cast<int>(percent);
    int h = static_cast<int>((percent - static_cast<float>(w)) * 100.0f);
    if (h < 0) h = 0;
    if (h > 99) h = 99;

    whole = w;
    hundredths = h;
    return true;
#else
    (void)whole; (void)hundredths;
    return false;
#endif
}

// Korok seeds found, out of the flag's declared maximum. This is the counter the
// completion percentage leans on for three quarters of its range.
inline bool GetKorokCount(int& out) {
#if !WIIXL_SWITCH
    return GameData::GetFlagS32(kKorokFlag, out);
#else
    (void)out;
    return false;
#endif
}

inline bool GetKorokTotal(int& out) {
#if !WIIXL_SWITCH
    return impl::FlagMax(kKorokFlag, out);
#else
    (void)out;
    return false;
#endif
}

// Sets Korok seeds found, clamped to the flag's declared maximum.
//
// A real, save-backed write - this IS your Korok count, not a display trick, so
// it moves Hestu's dialogue and the completion percentage alike.
inline bool SetKorokCount(int count) {
#if !WIIXL_SWITCH
    int max = 0;
    if (!impl::FlagMax(kKorokFlag, max)) return false;

    if (count < 0) count = 0;
    if (count > max) count = max;
    return GameData::SetFlagS32(kKorokFlag, count);
#else
    (void)count;
    return false;
#endif
}

// The band the percentage can be moved across by SetKorokCount alone - the
// percentage at zero seeds and at the flag's maximum, with every other term
// left where it is.
//
// On a vanilla save with nothing else done that is roughly 0.2% to 77%; with
// the shrines, quests and Divine Beasts done the whole band shifts upward. It
// is what tells you that 100% is not reachable by seeds alone.
inline bool GetReachableRange(float& lowest, float& highest) {
#if !WIIXL_SWITCH
    Breakdown breakdown;
    if (!GetCompletion(breakdown) || breakdown.total <= 0) return false;

    const int fixed = breakdown.collected - breakdown.koroks;
    lowest = static_cast<float>(fixed * 100) / static_cast<float>(breakdown.total);
    highest = static_cast<float>((fixed + breakdown.korokTotal) * 100) /
              static_cast<float>(breakdown.total);
    return true;
#else
    (void)lowest; (void)highest;
    return false;
#endif
}

// --- forcing the number on screen ---------------------------------------
//
// Everything above reads. This last section does not: it hooks the map's own
// completion update and repaints the digits, so the pane can read anything you
// like on a save that has earned nothing.
//
// It changes the DISPLAY and nothing else. No flag moves, no counter moves, and
// GetCompletionPercent goes on reporting the real figure - so after an override
// the two halves of this header deliberately disagree. Since nothing in the
// game ever reads the percentage back, there is nothing else for a "set" to
// mean here.

// Installs the hook. Call once from WiiXLaunch_Init(), before any of the
// SetDisplayed* calls; they do nothing until it has run.
//
// Installing does not by itself change anything - with no override set the hook
// runs the original and tests one bool.
//
// Returns false on Switch, where the target was never RE'd.
inline bool Init() {
#if !WIIXL_SWITCH
    if (impl::HookInstalled()) return true;
    impl::CompletionDisplayHook::Install(0x0, impl::kUpdateCompletionDisplayWiiU);
    impl::HookInstalled() = true;
    return true;
#else
    return false;
#endif
}

// Forces the number on screen, as the two parts the map prints: a whole part
// and hundredths. whole is clamped to 0-100 and hundredths to 0-99.
//
// forceVisible additionally re-shows the pane. The original hides it whenever
// GameClear is clear, so without this an override is invisible until the game
// has actually been beaten - which is usually what you want, and occasionally
// exactly what you do not.
//
// Takes effect on the map's next refresh, so an override set while the map is
// already open will not appear until it redraws.
inline bool SetDisplayedParts(int whole, int hundredths, bool forceVisible = false) {
#if !WIIXL_SWITCH
    if (!impl::HookInstalled()) return false;

    if (whole < 0) whole = 0;
    if (whole > 100) whole = 100;
    if (hundredths < 0) hundredths = 0;
    if (hundredths > 99) hundredths = 99;

    impl::OverrideWhole() = whole;
    impl::OverrideHundredths() = hundredths;
    impl::OverrideForceVisible() = forceVisible;
    impl::OverrideActive() = true;
    return true;
#else
    (void)whole; (void)hundredths; (void)forceVisible;
    return false;
#endif
}

// The same thing from a percentage. Split the way the UI splits its own -
// truncated, not rounded - so SetDisplayedPercent(GetCompletionPercent())
// reproduces the digits the map would have drawn anyway.
inline bool SetDisplayedPercent(float percent, bool forceVisible = false) {
#if !WIIXL_SWITCH
    if (!(percent == percent)) return false;   // NaN
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    const int whole = static_cast<int>(percent);
    const int hundredths =
        static_cast<int>((percent - static_cast<float>(whole)) * 100.0f);
    return SetDisplayedParts(whole, hundredths, forceVisible);
#else
    (void)percent; (void)forceVisible;
    return false;
#endif
}

// Drops the override. The map goes back to its own number on the next refresh.
//
// Does NOT re-hide a pane that forceVisible showed - the original will hide it
// again itself on that refresh, for the same reason it hid it before.
inline void ClearDisplayOverride() {
#if !WIIXL_SWITCH
    impl::OverrideActive() = false;
    impl::OverrideForceVisible() = false;
#endif
}

inline bool IsDisplayOverridden() {
#if !WIIXL_SWITCH
    return impl::OverrideActive();
#else
    return false;
#endif
}

} // namespace WiiXLaunch::BotW::Completion
