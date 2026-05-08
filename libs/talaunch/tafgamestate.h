#pragma once
#include <cstdint>

#pragma pack(1)
static const char*     TAFGAMESTATE_SHMEM_NAME = "TAFGameState";
static const uint32_t  TAFGAMESTATE_MAGIC       = 0x54414603u;  // ...02 = added KillEvent ring; ...03 = added tiebreakerWinnerDplayId

// IMPORTANT: per-slot arrays here are indexed by TA's local Players[0..9] order
// (each peer puts itself at slot 0). Do NOT cross-reference these arrays by
// lobby slot — resolve the player via playerDirectPlayId[xslot] instead.

// KillEvent flags (uint16_t bitfield in TAFKillEvent::flags). Events are emitted on
// every commander-death edge, not just dgun-victim — see exporter ScanCommanderDeaths.
static const uint16_t TAF_KILL_FLAG_VICTIM_COMMANDER = 0x0001u;
static const uint16_t TAF_KILL_FLAG_KILLER_COMMANDER = 0x0002u;
static const uint16_t TAF_KILL_FLAG_PRESUMED_DGUN    = 0x0004u; // both attacker and victim are commanders
                                                                // and victim died from this damage event
                                                                // (dgun is the only commander weapon
                                                                // that one-shots commanders in vanilla TA).
static const uint16_t TAF_KILL_FLAG_SELF_DESTRUCT    = 0x0008u; // victim == killer (engine sets Attacker_p
                                                                // to self for user-initiated self-destruct
                                                                // and self-AOE caught-in-own-blast).
                                                                // Excluded from tiebreaker "last death"
                                                                // logic — represents the cause, not the
                                                                // last commander death.

struct TAFKillEvent {
    int64_t  wallClockMs;     // GetSystemTimeAsFileTime-derived ms since unix epoch when death was observed
    uint32_t victimDplayId;   // PlayerStruct::DirectPlayID of the dying unit's owner; 0 if unknown
    uint32_t killerDplayId;   // PlayerStruct::DirectPlayID of the attacker's owner; 0 if attacker unknown
    uint16_t flags;           // see TAF_KILL_FLAG_* above
    uint16_t _pad;
}; // 20 bytes

static const int TAF_KILL_RING_SIZE = 16;

struct TAFGameState {
    uint32_t magic;                    // TAFGAMESTATE_MAGIC when data is valid
    uint32_t sequenceNumber;           // seqlock: odd=writing, even=stable, 0=never written
    uint8_t  playerAllyFlags[10][10];  // playerAllyFlags[i][j] != 0 => slot i is allied with slot j
                                       // mirrors PlayerStruct.AllyFlagAry; irrelevant if playerActive[i]==0
    uint8_t  playerAllyTeam[10];       // PlayerStruct.AllyTeam (0-4 = explicit team, 5 = none)
    uint8_t  playerActive[10];         // 1 = slot occupied, 0 = empty.
                                       // SLOT OCCUPANCY ONLY — does NOT reflect elimination.
    int16_t  playerUnitsNumber[10];    // engine's live unit count per slot. Consumers derive
                                       // elimination via a max-seen-then-zero edge latch.
    uint16_t playerPropertyMask[10];   // PlayerInfoStruct.PropertyMask (WATCH=0x40, HUMANPLAYER=0x80, PLAYERCHEATING=0x2000)
    uint32_t playerDirectPlayId[10];   // PlayerStruct.DirectPlayID

    // Kill-event ring buffer (mirror of the producer's; see tdraw's tafgamestate.h for protocol).
    uint32_t     killRingHead;                  // monotonically increasing event count
    TAFKillEvent killRing[TAF_KILL_RING_SIZE];

    // Tiebreaker decision computed by tdraw when mutual-elim is detected.
    // 0 = no decision yet. Otherwise: dplayId of the winning player.
    uint32_t     tiebreakerWinnerDplayId;
};
#pragma pack()
