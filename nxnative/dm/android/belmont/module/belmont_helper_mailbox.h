// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Module-side client for the Belmont GUEST HELPER mailbox (format-2 load plan;
// see native/helper/README.md for the full design and native/helper/
// belmont_guest_helper.S for the guest side). The byte offsets below are the
// single source of truth shared with the assembly: belmont_guest_helper.S
// encodes every one of them as a literal immediate, so a change here needs the
// matching change there (and vice versa) -- the layout_check struct at the
// bottom of this file makes a silent drift a compile error.
//
// This header never touches EdenDsmodHostExtensions directly: the Client below
// is templated on the four primitives the host extension already provides
// (load_u32/load_u64/store_u32/store_u64, offset-addressed, reads acquire,
// stores release -- dsmod_module_extensions.h), the same style
// belmont_pin_transaction.h uses for Read/Write. That keeps this header, and
// its unit test (belmont_helper_mailbox_test.cpp, a fake in-memory mailbox),
// independent of the runtime SDK's include path. The integrator wires real
// `configure()`-provided function pointers into Client's constructor (see
// native/helper/README.md "Integration edits").

#include <cstddef>
#include <cstdint>
#include <utility>

namespace belmont_helper_mailbox {

inline constexpr std::uint32_t MailboxSize = 4096;
inline constexpr std::uint32_t EpochOffset = 4092; // mailbox_epoch_offset in the load plan

// --- Byte offsets (see native/helper/README.md "Mailbox layout" for the prose
//     table this mirrors). ---
inline constexpr std::uint32_t OffsetMagic = 0x00;
inline constexpr std::uint32_t OffsetHeartbeat = 0x04;
inline constexpr std::uint32_t OffsetReqSeq = 0x08;
inline constexpr std::uint32_t OffsetReqOp = 0x0C;
inline constexpr std::uint32_t OffsetReqEpoch = 0x10;
// 0x14 reserved (4 bytes)
inline constexpr std::uint32_t OffsetReqPrimaryPtr = 0x18;
inline constexpr std::uint32_t OffsetReqSecondaryPtr = 0x20;
inline constexpr std::uint32_t OffsetReqTertiaryPtr = 0x28;
inline constexpr std::uint32_t OffsetReqArgA = 0x30;
inline constexpr std::uint32_t OffsetReqArgB = 0x34;
inline constexpr std::uint32_t OffsetReqArgC = 0x38;
// 0x3C reserved (4 bytes)
inline constexpr std::uint32_t OffsetCtxMarkersComponent = 0x40;
inline constexpr std::uint32_t OffsetCtxInventory = 0x48;
// 0x50, 0x58 reserved (16 bytes)
inline constexpr std::uint32_t OffsetResultSeq = 0x60;
inline constexpr std::uint32_t OffsetResultCode = 0x64;
inline constexpr std::uint32_t OffsetResultValue = 0x68;
inline constexpr std::uint32_t OffsetClassTable = 0x70; // ClassTableSlots * u64
inline constexpr std::uint32_t ClassTableSlots = 16;
// 0xF0 .. 0xFFB reserved (3852 bytes); 0xFFC (EpochOffset) is the plan's epoch word.

inline constexpr std::uint32_t MagicValue = 0x32504C42; // belmont_guest_helper.S movz/movk

// class_table[] indices. Fixed per opcode in belmont_guest_helper.S (not supplied
// per-request): the module fills every slot once, as soon as each klass pointer
// is known, and never needs to move them again (IL2CPP klass pointers are
// process-lifetime constants).
enum class ClassSlot : std::uint32_t {
  MapMarkerData = 0,
  LevelID = 1,
  MapMarkerTypeID = 2,
  SpellItemData = 3,
  WeaponItemData = 4,
  CharmItemData = 5,
  MapMarkersComponent = 6,
  Inventory = 7,
};

enum class Op : std::uint32_t {
  None = 0,
  AddPin = 1,
  RemovePin = 2,
  ChangePin = 3,
  EquipSpell = 4,
  EquipWeapon = 5,
  EquipCharm = 6,
};

// Mirrors the refusal codes belmont_guest_helper.S's acknowledge_* labels emit.
enum class Result : std::uint32_t {
  Ok = 0,
  NotConfigured = 1,         // magic/class_table slot not seeded yet
  BadOp = 2,                 // req_op outside [1,6]
  StaleEpoch = 3,             // req_epoch != the mailbox epoch word, or the word is 0
  Loading = 4,                 // ApplicationFlowManager._isLoading != 0, or unresolved
  NullContext = 5,              // ctx_markers_component/ctx_inventory null or class mismatch
  NullPrimary = 6,
  NullSecondary = 7,
  NullTertiary = 8,
  ClassMismatchPrimary = 9,
  ClassMismatchSecondary = 10,
  ClassMismatchTertiary = 11,
  NotOwned = 12,               // equip item not found in Inventory._itemsInInventory
  AllocFailed = 13,             // ADD_PIN's il2cpp object-new returned null
};

struct Request {
  Op op{Op::None};
  std::uint32_t epoch{};
  std::uint64_t primary_ptr{};
  std::uint64_t secondary_ptr{};
  std::uint64_t tertiary_ptr{};
  std::uint32_t arg_a{};
  std::uint32_t arg_b{};
  std::uint32_t arg_c{};
};

struct Outcome {
  bool answered{};
  Result code{Result::NotConfigured};
  std::uint64_t value{};
};

// LoadU32/LoadU64: bool(uint32_t offset, T* out). StoreU32/StoreU64: bool(uint32_t
// offset, T value). Bind the real EdenDsmodHostExtensions::load_*/store_* (plus
// their `void* userdata`) into these via a lambda at the call site.
template <class LoadU32, class LoadU64, class StoreU32, class StoreU64>
class Client {
public:
  Client(LoadU32 load32, LoadU64 load64, StoreU32 store32, StoreU64 store64)
      : load32_(std::move(load32)), load64_(std::move(load64)),
        store32_(std::move(store32)), store64_(std::move(store64)) {}

  // Call once, right after configure(). Seeds magic + a non-zero epoch so the
  // guest helper does not spend every tick refusing before SetClass/SyncEpoch
  // have run even once.
  bool Initialize(std::uint32_t initial_epoch = 1) {
    if (initial_epoch == 0) {
      initial_epoch = 1;
    }
    bool ok = true;
    ok = store32_(OffsetResultSeq, 0) && ok;
    ok = store32_(OffsetReqSeq, 0) && ok;
    ok = store32_(EpochOffset, initial_epoch) && ok;
    ok = store32_(OffsetMagic, MagicValue) && ok; // last: marks the mailbox usable
    return ok;
  }

  bool SetClass(ClassSlot slot, std::uint64_t klass_ptr) {
    return store64_(OffsetClassTable + static_cast<std::uint32_t>(slot) * 8, klass_ptr);
  }

  // Refresh the per-tick context (current MapMarkersComponent*/Inventory*).
  // Call every module tick, before TryStage: the guest re-validates these
  // pointers' klass itself immediately before calling, closing the race
  // between the module resolving them and the guest acting on them.
  bool SetContext(std::uint64_t markers_component, std::uint64_t inventory) {
    bool ok = true;
    ok = store64_(OffsetCtxMarkersComponent, markers_component) && ok;
    ok = store64_(OffsetCtxInventory, inventory) && ok;
    return ok;
  }

  // Keep in sync with get_i64("__mailbox_epoch") (ModRuntime::SourceQuery /
  // System::GetDualScreenGuestMailboxEpoch) so a debugger pause/invalidate
  // between staging and processing is detected by the guest as staleness
  // rather than silently acted on. A host_epoch of 0 means "not bumped yet":
  // keep this client's own seed rather than writing a value the guest treats
  // as "never initialized".
  bool SyncEpoch(std::uint32_t host_epoch) {
    if (host_epoch == 0) {
      return true;
    }
    return store32_(EpochOffset, host_epoch);
  }

  // Current epoch word (host pause/invalidate bumps it in place). Stage each
  // request with this value; 0 means unreadable/never seeded.
  std::uint32_t Epoch() {
    std::uint32_t value{};
    return load32_(EpochOffset, &value) ? value : 0;
  }

  // True once the guest has ticked at least once since the previous call.
  bool IsAlive() {
    std::uint32_t value{};
    if (!load32_(OffsetHeartbeat, &value)) {
      return false;
    }
    const bool alive = value != last_heartbeat_;
    last_heartbeat_ = value;
    return alive;
  }

  bool Busy() const {
    return pending_;
  }

  // Stages exactly one request. Refuses while a previous one is still
  // in flight (H6: never ask the guest to run more than one native mutator
  // per tick); call Poll() first to drain it.
  bool TryStage(const Request& request) {
    if (pending_) {
      std::uint32_t result_seq{};
      if (!load32_(OffsetResultSeq, &result_seq) || result_seq != pending_seq_) {
        return false;
      }
      pending_ = false;
    }
    if (request.op == Op::None) {
      return false;
    }
    const std::uint32_t seq = NextSeq();
    bool ok = true;
    ok = store64_(OffsetReqPrimaryPtr, request.primary_ptr) && ok;
    ok = store64_(OffsetReqSecondaryPtr, request.secondary_ptr) && ok;
    ok = store64_(OffsetReqTertiaryPtr, request.tertiary_ptr) && ok;
    ok = store32_(OffsetReqArgA, request.arg_a) && ok;
    ok = store32_(OffsetReqArgB, request.arg_b) && ok;
    ok = store32_(OffsetReqArgC, request.arg_c) && ok;
    ok = store32_(OffsetReqEpoch, request.epoch) && ok;
    ok = store32_(OffsetReqOp, static_cast<std::uint32_t>(request.op)) && ok;
    ok = store32_(OffsetReqSeq, seq) && ok; // release: must be written last
    if (!ok) {
      return false;
    }
    pending_ = true;
    pending_seq_ = seq;
    return true;
  }

  // Non-blocking. Outcome::answered is false while the request from the most
  // recent successful TryStage is still in flight.
  Outcome Poll() {
    if (!pending_) {
      return {};
    }
    std::uint32_t result_seq{};
    if (!load32_(OffsetResultSeq, &result_seq) || result_seq != pending_seq_) {
      return {};
    }
    std::uint32_t code_raw{};
    std::uint64_t value{};
    load32_(OffsetResultCode, &code_raw);
    load64_(OffsetResultValue, &value);
    pending_ = false;
    return {true, static_cast<Result>(code_raw), value};
  }

private:
  std::uint32_t NextSeq() {
    if (++next_seq_ == 0) { // never stage seq 0: both req_seq/result_seq start at 0
      next_seq_ = 1;
    }
    return next_seq_;
  }

  LoadU32 load32_;
  LoadU64 load64_;
  StoreU32 store32_;
  StoreU64 store64_;
  std::uint32_t next_seq_{0};
  std::uint32_t last_heartbeat_{0};
  bool pending_{false};
  std::uint32_t pending_seq_{0};
};

template <class LoadU32, class LoadU64, class StoreU32, class StoreU64>
Client<LoadU32, LoadU64, StoreU32, StoreU64> MakeClient(LoadU32 load32, LoadU64 load64,
                                                        StoreU32 store32, StoreU64 store64) {
  return Client<LoadU32, LoadU64, StoreU32, StoreU64>(std::move(load32), std::move(load64),
                                                       std::move(store32), std::move(store64));
}

// Compile-time proof that the offsets above agree with each other (never used
// for real I/O: the host extension ABI is offset-based, not struct-based, and
// the guest side is raw assembly -- this is documentation with teeth).
namespace layout_check {
#pragma pack(push, 1)
struct Mailbox {
  std::uint32_t magic;
  std::uint32_t heartbeat;
  std::uint32_t req_seq;
  std::uint32_t req_op;
  std::uint32_t req_epoch;
  std::uint32_t reserved0;
  std::uint64_t req_primary_ptr;
  std::uint64_t req_secondary_ptr;
  std::uint64_t req_tertiary_ptr;
  std::uint32_t req_arg_a;
  std::uint32_t req_arg_b;
  std::uint32_t req_arg_c;
  std::uint32_t reserved1;
  std::uint64_t ctx_markers_component;
  std::uint64_t ctx_inventory;
  std::uint64_t reserved2;
  std::uint64_t reserved3;
  std::uint32_t result_seq;
  std::uint32_t result_code;
  std::uint64_t result_value;
  std::uint64_t class_table[ClassTableSlots];
  std::uint8_t reserved4[MailboxSize - 0xF0 - 4];
  std::uint32_t epoch;
};
#pragma pack(pop)
static_assert(offsetof(Mailbox, magic) == OffsetMagic);
static_assert(offsetof(Mailbox, heartbeat) == OffsetHeartbeat);
static_assert(offsetof(Mailbox, req_seq) == OffsetReqSeq);
static_assert(offsetof(Mailbox, req_op) == OffsetReqOp);
static_assert(offsetof(Mailbox, req_epoch) == OffsetReqEpoch);
static_assert(offsetof(Mailbox, req_primary_ptr) == OffsetReqPrimaryPtr);
static_assert(offsetof(Mailbox, req_secondary_ptr) == OffsetReqSecondaryPtr);
static_assert(offsetof(Mailbox, req_tertiary_ptr) == OffsetReqTertiaryPtr);
static_assert(offsetof(Mailbox, req_arg_a) == OffsetReqArgA);
static_assert(offsetof(Mailbox, req_arg_b) == OffsetReqArgB);
static_assert(offsetof(Mailbox, req_arg_c) == OffsetReqArgC);
static_assert(offsetof(Mailbox, ctx_markers_component) == OffsetCtxMarkersComponent);
static_assert(offsetof(Mailbox, ctx_inventory) == OffsetCtxInventory);
static_assert(offsetof(Mailbox, result_seq) == OffsetResultSeq);
static_assert(offsetof(Mailbox, result_code) == OffsetResultCode);
static_assert(offsetof(Mailbox, result_value) == OffsetResultValue);
static_assert(offsetof(Mailbox, class_table) == OffsetClassTable);
static_assert(offsetof(Mailbox, epoch) == EpochOffset);
static_assert(sizeof(Mailbox) == MailboxSize);
} // namespace layout_check

} // namespace belmont_helper_mailbox
