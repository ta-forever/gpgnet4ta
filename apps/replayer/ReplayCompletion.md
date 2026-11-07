# Replay completion

TAF playback uses synthetic DirectPlay players. File EOF used to leave the
feeder in PLAY indefinitely; `doPlay()` never completed. The viewer is not a
recorded contestant, and synthetic peers cannot perform TA's final-statistics
exchange (`0x28` snapshots and `0x29` acknowledgements). Neither consuming the
file nor detecting elimination requests the viewer's native results transition.

Completion has three distinct boundaries:

1. **Producer finished.** A local replay file is finite. A network replay is
   live until an explicit `ReplayServerData` status **5 (REPLAY_COMPLETE)**.
   That frame can contain final data, which is flushed before the completion
   signal. Empty OK frames, short reads and disconnections are not completion.
2. **All records delivered.** The parser reaches a short read and the feeder's
   pending queue is empty. Playback packets use DirectPlay's guaranteed ordered
   delivery with their existing TA envelopes and original simulation timestamps.
   Each output player then sends a completion fence in the same ordered stream.
3. **TA processed the final events.** Chat fences and unit updates share TA's
   per-sender dispatch queue (`0x462f30` / `0x463790`). The matching tdraw receiver
   observes them at the native queue's dequeue point (`0x453da8`), before the
   dispatcher rejects an inactive sender. Known peer identities are retained
   across recorded departures. It waits for every queue's fence and a completed
   local simulation pass. A hook at simulation entry (`0x495490`) distinguishes
   events processed in that pass from paused/out-of-pass network polling, which
   must wait for a subsequent simulation tick. The existing GameTickHook runs after
   Game_RunSimulationStep (`0x495490`), including pending unit-death processing.
   Recorded `0x2c` ticks are a different clock, which diverges from local GameTime
   during accelerated playback; their absolute values must not be compared.
   It requests native end-game handling with GameStateMask bit 4,
   retaining any existing verdict. Only then may the native transition's obsolete
   network-statistics wait be skipped. The native statistics builder, end-screen
   state machine, buttons, and teardown still run.

The feeder continues draining its DirectPlay receive queue in DONE. Finishing
the recording does not finish the session: TA must still close its peer through
native teardown, and the viewer must retain control of the results interface.

## Output-only capability and fence, versions 1 and 2

This is a 65-byte empty `CHAT_05` extension, registered as ChatHijackId 0x32 in
tdraw. All unspecified bytes are zero.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 1 | `0x05` |
| 1 | 1 | NUL (not visible chat) |
| 2 | 1 | `0x32` |
| 3 | 1 | 16 for v1, 17 for v2 (extension payload size) |
| 4 | 1 | Version: 1 or 2 |
| 5 | 3 | `BEG` capability announcement (v2) or `END` completion fence |
| 8 | 4 | Maximum original simulation tick, little endian |
| 12 | 2 | Required output-token bit mask, little endian |
| 14 | 2 | This output player's token bit, little endian |
| 16 | 1 | v2: 0 for an extra viewer, 1 when the local slot represents the tenth recorded player |

Before sending recorded game events, the producer announces v2 capability in
each ordered output queue. This enables the receiver's explicit EOF wait without
counting as completion. Legacy replayers, including SY, send no announcement and
keep their native ending path. A late announcement from another queue cannot
clear already received completion fences. The producer emits v2; the receiver also accepts v1. A v2 producer needs the
updated paired receiver. The local role comes from the recording's invisible
player mapping, independent of player name, selected POV, or live slot count.
The native statistics builder skips only an extra local viewer, before copying
statistics or calculating graph scales. Native row layout compacts visible rows
without changing their player/colour mapping. All ten recorded players remain
eligible, including a tenth player that produced no funnel packets. With a v1
producer or a manual early exit, the existing native funnel identity is used.

Tokens come from recorded colour IDs; they are not native player-array indices.
The receiver resolves each source by DirectPlay ID, binds each token to a unique
output sender, requires consistent masks/ticks, and accepts fences only during
replay playback. A ten-player replay's funnel shares the host's raw transport,
but TA rewrites it into a separate native per-player queue. That queue receives
its own fence using the existing funnel/LOS marker; the mask includes all ten
recorded tokens. Recorded fences from an earlier
playback are filtered rather than allowed to close a later playback prematurely.

Automatic native victory/leave handling waits for terminal handoff and simulation
drain, so it cannot cut off later recorded final statistics. Confirmed user exits and genuine transport
loss retain their native exit path. The responsive transport closer is armed for
native replay endings as well as explicit feeder completion, so an existing native
ending from a compatible feeder cannot fall back to the synchronous GUI-thread network close.

The system DirectPlay provider's blocking Close runs on a retained worker only
after native HAPINET_close and its GUI-side Release calls have returned. The
recorder finalization, statistics snapshot, unit cleanup and fade stay on the
GUI thread. Interface and module references remain pinned until Close finishes.
A genuine Main Menu request made during cleanup is retained and resumed through
the native GUI handler after cleanup; results remain responsive in the meantime.

## Compatibility and incomplete information

Existing TAD layouts and event timestamps are unchanged. This completion feature
requires the paired tdraw receiver; an older DLL will ignore the empty-chat
extension. Legacy live servers without status 5 still support growing playback,
but cannot establish a safe automatic ending from their existing protocol.
Deploying streamed completion requires server support for the terminal status.

A truncated final record is not dispatched. Complete records before it can
finish, with a warning in the replayer log. Statistics come from TA's state after
those records, including any recorded final snapshots. Missing snapshots are
not synthesized, and no commander deaths, player eliminations or victory
verdicts are manufactured to finish playback. The results stay open until the
viewer uses TA's usual interface.
