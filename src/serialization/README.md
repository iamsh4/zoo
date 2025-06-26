# Session
In `zoo`, a Session is a sequence of Snapshots (see below) and a timeline of all user inputs.

Session provides a timeline of full or partial system states. Execution of the guest system may
begin at its initial "reset" state, or at any full snapshot within the session. If the user
wishes to go to some past point T in the timeline, they most load the most recent state before
T and then deterministically replay all external inputs from that point forward until time T.

From this, it is clear that any time in the past can be reached by simply resetting the system
with no save states and playing all user inputs. By taking additional or periodic snapshots, it
is possible to make efficient "rewind" or random timeline seeking possible, subject to limits
on memory or disk storage for snapshots within a session.

Sessions are intended to be reload-able across multiple program states and also portable across
host platforms.

# Snapshot
Snapshot is a state of all the emulated system "components" at a moment in time. It is equivalent
to a "save state". Each Snapshot component is named and refers to a buffer of data with a given
length. Snapshot serialization/deserialization ("serdes") is performed by a class implementing two
methods in `serialization::Serializer`.

Note that conceptually Snapshots are either "full" or "partial". A full snapshot may be deserialized
in isolation. A partial snapshot is a set of state applied "on top of" another snapshot (which itself
may be a partial snapshot referring to other snapshots etc.). The purpose of partial snapshots is to
enable more smaller snapshots without requiring the full system state to be copied into each snapshot
in situations where only a small amount of data has been modified.

# Thought: Linear vs Tree Sessions
Snapshots are effectively nodes in a graph, potentially also containing some inputs which continue from
that point onward. With the user being able to rewind, and provide new inputs and then save again,
it introduces a question of how to handle "conflicts". There are a few natural approaches:
1. If there is a snapshot "in the future" relative to the current time, then don't allow saving.
2. If there is a snapshot "in the future" relative to the current time, then any save state deletes
all future snapshots, effectively stating "this is the new timeline from here forward." This is being
referred to as a **Linear Session**.
3. The most general approach instead is to realize all save states as a complete graph, so that when
a user saves and there is already some future snapshot then they are now beginning a new branch off
of the most recent snapshot in the past. This effectively saves and allows for anything, but presents
challenges in UI.

# v1 Session and State Management
In Penguin V1, the following will be the user experience for sessions and snapshots/states:
- All snapshots in the session are either explicitly or implicitly captured. The distinction is meaningful
  when users are browsing snapshots. Explicitly captured snapshots are never automatically deleted.
- Any snapshot can be "bookmarked", optionally given a name. 
- Any snapshot (in isolation) can be exported as a bare snapshot for loading outside of a session. This is
  useful for sharing a single snapshot / giving a snapshot during filing a bug / etc.
- When navigating backward in time and then saving while there are snapshots in the future on the
  current branch, all future snapshots on that branch are forgotten/deleted.
- A user may easily "seek" backwards and forward in snapshots.

Session
- Metadata
- Bookmarks[]
- snapshots/*
