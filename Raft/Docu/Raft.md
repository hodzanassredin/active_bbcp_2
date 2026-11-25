# Raft: a replicated log

## Why

An account lives in an event journal, and the journal lives on one machine. The machine dies and the
account is gone. Put the journal on three machines and write to all three. Now the network splits: two
machines see each other, the third sees nobody, and its client writes too. When the network heals, one
position of the journal has two different values. This is exactly the limit of stage 4 of the actors
(`ActorGrain`): under a partition a key could get two incarnations, and nothing told which one was right.

Raft solves it this way: at any moment only the leader writes, and an entry counts as done (committed)
when a majority of the nodes has stored it. Two majorities of three (or five) always intersect, so two
leaders cannot commit different entries at one position: the one left in the minority has no majority,
and its entries stay uncommitted. The `Raft` subsystem follows the extended Raft paper (Ongaro,
Ousterhout, "In Search of an Understandable Consensus Algorithm") and its author's dissertation.

## How it works

### The core is a state machine without I/O

`RaftCore.Node` is a node as an automaton. It writes no disk, sends nothing and knows no clock. Three
inputs change it:

- `Step(nd, m)` — a message from another node arrived;
- `Tick(nd)` — a clock tick; the leader sends a heartbeat on every tick, a follower starts an election
  after a random number of ticks without the leader (`electionTicks` to `2*electionTicks - 1`);
- `Propose(nd, data)` — a client command; only the leader accepts it.

Everything the node wants done in the world goes to a `Ready`. The owner takes it with `GetReady`, does
it and reports `Advance`:

```
WHILE RaftCore.HasReady(nd) DO
	RaftCore.GetReady(nd, r);
	storage.Save(nd, r, res);	(* term, vote, new entries; fsync before return *)
	send r.msgs;
	apply entries r.applyLo .. r.applyHi to the state machine;
	answer the reads r.reads;
	RaftCore.Advance(nd, r)
END
```

The order matters: save first, send second (`r.syncFirst`). A node that voted, sent the vote and crashed
before saving forgets the vote after restart and votes a second time in the same term — the term gets two
leaders. The test `Raft_devSim.TestMutLostVote` plays exactly this.

The automaton is deterministic: the random election timeout comes from the node's seed. A simulation
run with the same inputs repeats step for step, and the core runs without network and disk.

### Elections and the log

Time is divided into terms. A follower that hears no leader increments its term, votes for itself and
asks the others. A node gives one vote per term, and only to a candidate whose log is at least as
up to date as its own (§5.4.1 of the paper). Without this rule a node that never saw a committed entry
could become leader and erase that entry elsewhere.

The leader sends `AppendEntries` with the index and term of the previous entry (`prevIndex`,
`prevTerm`). A follower accepts only if it has that previous entry; otherwise it refuses with a hint where
to start, and the leader backs off. If a new entry conflicts with an existing one (same index, other
term), the follower drops its own entry and the whole tail after it. So logs converge to the leader's.

The leader commits an entry when a majority holds it — but only an entry of its own term; entries of
earlier terms are committed together with it (§5.4.2, figure 8 of the paper). Otherwise an old-term
entry copied to a majority could be erased by the next leader with a newer last term. A new leader
writes an empty entry of its term at once; it commits what was inherited. `TestMutFigure8` plays
figure 8 and catches commitment by counting replicas.

### State machine and clients

Committed entries are applied to a state machine (`RaftSm.StateMachine`: `Apply(index, command)` →
result, `Snapshot`, `Restore`) on every node in the same order, so the copies agree.

A client that got no answer retries — perhaps with a new leader — while the first attempt may already be
in the log. `RaftSm.Sessions` prevents double application: a command carries a client number and an
operation number (`RaftSm.Encode`); the session remembers the client's last applied number and result and
answers a retry with the same result. A client sends one operation at a time.

Reads do not go through the log; they use ReadIndex (§6.4 of the dissertation): the leader remembers its
`commit`, sends a heartbeat and waits until a majority answers — then it is surely still the leader; the
read may proceed once the state machine has applied up to the remembered index.
`RaftCore.ReadIndex(nd, ctx)` accepts a read, `Ready.reads` returns the confirmed ones. A leader cut off
by a partition gathers no majority and returns nothing stale. A leader that has not committed an entry of
its own term refuses reads: its `commit` may lag.

### Snapshots

The log grows without end. `RaftCore.Compact(nd, index, snap)` replaces the applied and committed prefix
with a snapshot of the state machine; a node's snapshot is `Sessions.Snapshot` (sessions together with
the machine: without them a retry after a snapshot would apply again). To a lagging node that needs
discarded entries the leader sends the snapshot (`snapReq`, InstallSnapshot); the follower returns it in
`Ready.snap`, and the owner saves it and restores the machine from it.

### Storage

`RaftStorage.Storage`: `Save(nd, ready)`, `Compact`, `Load`. `RaftStorage.NewMem()` keeps state in memory
for tests. `RaftKvStorage.New(db)` keeps it in a Kv database: one write transaction per `Ready`, and
`KvDb.Tx.Commit` does fsync. Everything the leader accepted between two `Ready`s goes in one commit
(group commit). An interrupted commit is invisible: after a power cut `Load` sees the previous state
whole.

### Node actor

`RaftNode` drives the state machine on top of actors: a Raft node is the root actor of an `ActorNet` node
(`RaftNode.Start(cfg)`) with its own executor task. The `Config` gives the node number and the cluster
size, the `ActorNet` node, the storage, the state-machine factory and the peers' capabilities
(`ActorNet.Export`, the array `peers` by number). Raft messages between nodes are `ActorNet` frames: term,
indexes, entries and the snapshot are encoded in the frame body.

The actor's inputs are a tick (`RaftNode.Tick`; with `tickMs > 0` its own main-loop clock sends it,
otherwise the owner does), a peer's message, a `Propose` command and a `Query` read. After every input
the driver executes `Ready` in the order "save (`RaftStorage.Save`) → send → apply (`RaftSm.Sessions`) →
`Advance`". The client receives a `Reply`:

- to `Propose` — after the entry is committed and applied on this node: `res = ok`, the result of the
  state machine and the entry index;
- to `Query` — after ReadIndex is confirmed: the answer of `StateMachine.Query` (by default the snapshot);
- `notLeader` with the leader's number in `leader` — the node is not the leader, ask the one named;
- `lost` — another entry ended up at the command's index (the leader changed): repeat the same operation
  with the same number, the session will not apply it twice. A lost answer is cured by the same retry.

A storage failure breaks the node: it stops answering and stops itself; it is brought back by a new
`Start` after `ActorNet.Up`, with the state from the storage. Example: `ObxRaft0`.

### Journal of persistent actors

`RaftJournal` turns Raft into an `ActorJournal.Journal`. The state machine `RaftJournal.Machine` keeps a
copy of the journal (`ActorJournal.NewMem`) on every node; the commands "append an event" and "save a
snapshot" go through the Raft log, and the check of the expected number (`expected`) runs when the
command is applied — one check for the whole cluster. `RaftJournal.Client` goes into
`ActorPersist.Actor.journal`. Behind the client is an agent actor on node `net` (named
`$journal<id>`): it sends `Propose` commands to the leader, looks for the leader, retries on timeout and
answers the journal request by a message. A persistent actor uses the asynchronous methods (`Submit`,
`SubmitSnapshot`, `Replay`, ADR 0026): `Persist` does not wait; the event is applied and `Persisted` is
called when a majority has committed the entry, and messages are stashed until then. `Replay` is one
`Query` read: the snapshot and the events after it. The synchronous `Append`, `Last`, `Read`,
`SaveSnapshot`, `LoadSnapshot` remain for code outside an actor: they wait for the agent's answer in
`AsyncChan.Select`.

The agent's clock: `tickMs > 0` (default 10) is its own clock with timeouts `timeoutMs` and `giveUpMs`;
`tickMs = 0` means the owner sends ticks by calling `RaftJournal.Tick(c)`, with timeouts `timeoutTicks`
and `giveUpTicks` in ticks. So the journal works under `ActorSim` too: the driver ticks the client
together with the nodes, and a run is a function of the seed. The journal client number is new for every
`Client` incarnation. `failed` from a write means "unknown" (no answer within `giveUp`): `ActorPersist`
traps, and the actor's restart re-reads the journal. `Close` stops the agent. Example: `ObxRaft1`.

## Testing

`Raft_devSim` is a deterministic simulation of 3 and 5 nodes. Messages are lost, duplicated and
reordered (within a window of the eight oldest), nodes crash — in the middle of a `Ready` between saving
and sending, and in Kv mode in the middle of a commit with a `KvFault` power cut; the network splits into
islands (minority and majority) and in one direction; node clocks run at different rates; counter
clients retry and read. After every step the test checks:

- at most one leader per term;
- log matching: the same term at index i on two nodes means the same entries 1 .. i;
- leader completeness: a new leader holds all committed entries;
- what is committed and applied at index i is the same entry on all nodes and never changes;
- at the end, with the network healed, the cluster commits again;
- the client history is linearizable (a counter: increment returns the new value, read the current one).

The regular run is 500 seeds on 3 nodes and 500 on 5, 2000 steps each, and 60 seeds on Kv (about
15 s). The long one is `Raft_devSim.Long`: 3000 + 3000 seeds of 3000 steps (about a minute). A failure
prints the seed; a step-for-step replay, from the install directory (`tools/run64.sh` passes no
parameters):

```
echo 'Raft_devSim.Replay' | ./blackbox-console --RAFT_SEED=<seed> --RAFT_N=5 --RAFT_STEPS=3000
```

Eight mutations of the algorithm are switched on by `RaftCore.mutations` (tests only) and must be caught:
a vote without the up-to-date check, committing an old-term entry by counting replicas, answering before
saving, keeping a conflicting tail, two votes in a term, applying uncommitted entries, reading without
confirming leadership, retrying without a session.

`Raft_devNode` tests the driver: `RaftNode` nodes on `ActorNetMem` with lost, duplicated, delayed and
reordered frames, network cuts (islands and one-way), node crashes (the leader more often) and storage
failures in the middle of a `Ready`; clients are actors on a separate node and retry and read. The
executor is `ActorSim`, so a run is a function of the seed. The invariants are those of `Raft_devSim`
plus one more: a command the client got "yes" for is among the committed entries. The regular run is
200 seeds on 3 nodes and 60 on 5, 3000 moves each; the long one is `Raft_devNode.Long` (1000 + 300 seeds
and a mutation search). The driver mutations `RaftNode.mutations` are caught: sending before saving
(`mutSendFirst`) — "a new leader without a committed entry", answering the client before commit
(`mutEarlyReply`) — "a confirmed command is lost".

`Raft_devPersist` tests persistent actors on `RaftJournal` under `ActorSim` by seed: 3 Raft nodes on
`ActorNetMem` with frame loss, duplicates and delays, node crashes and network partitions; persistent
counters write, die with their node and recover on another one, sometimes two incarnations write to one
stream. Invariants: a confirmed event is in the journal; the stream numbers go without gaps; each event
was written from the state right before it (no interleaving of two incarnations); after recovery the
actor's state equals the fold of the journal. The normal run is 300 seeds of 1500 steps (about 2 s); the
long one is `Raft_devPersist.Long` (2000 seeds of 3000 steps and the mutation search). Mutations are
caught: `RaftJournal.mutNoWait` on seed 1, `ActorPersist.mutNoStash` on seed 1,
`RaftJournal.mutNoExpected` on seed 9.

`Raft_devJournal` tests `RaftJournal` on the `ActorRun` pool in real time: a persistent counter writes and
recovers from the Raft journal, the leader's death loses nothing confirmed, and a leader cut off from the
majority does not confirm a write (Append returns through the new leader once the entry is on a
majority). The mutation `RaftJournal.mutNoWait` (answering "written" without waiting) is caught by this
scenario.

## Cost

`Raft_devBench.Run` (3 nodes in one process, a FIFO network without faults; measured, amd64 Linux): in
memory 250 thousand commands per second with one client and about 450 thousand with 64; on Kv with
fsync 207 commands per second with one client (each waits for three commits of two fsyncs), 1600 with 8
and 11 thousand with 64, because 64 commands go in one commit per node (0.05 saves per command against
3). An election after a leader crash takes 12 ticks on average, at most 17 (timeout 10..19); with a
10 ms tick that is about 120 ms (derived). A node 100 000 commands behind catches up by a snapshot in
15 ms in memory.

## What is missing

- The `RaftNode` actor is tested on `ActorNetMem` (with faults) and in the examples; it was not run on
  `ActorNetTcp` between processes.
- The `RaftJournal` journal under `ActorSim` is tested only on `ActorNetMem`; `ActorGrain` was not tested
  on it. A mailbox with `Props.capacity > 0` may lose the journal's answer — a persistent actor on Raft
  needs an unbounded mailbox. The journal state machine's
  snapshot is all accepted commands in a row; there is no separate truncation for it.
- No membership change: the cluster size is fixed at creation.
- No PreVote and no CheckQuorum: a node behind a partition raises its term and deposes the leader when it
  returns; an old leader in a minority believes it leads until it hears a higher term (it can neither
  commit nor read).
- Client sessions are never evicted.

## Interface

`RaftCore`: `New`, `Tick`, `Step`, `Propose`, `ReadIndex`, `HasReady`, `GetReady`, `Advance`, `Compact`,
`SetBatch`, `TermAt`, `EntryAt`; node fields `id`, `n`, `term`, `vote`, `role`, `leader`, `last`, `commit`,
`applied`, `snapIndex`, `snapTerm`.
`RaftSm`: `StateMachine` (`Apply`, `Snapshot`, `Restore`), `Sessions` (`Apply`, `Snapshot`, `Restore`),
`New`, `Encode`, `Decode`.
`RaftStorage`: `Storage` (`Save`, `Compact`, `Load`), `NewMem`. `RaftKvStorage`: `New(db)`.
`RaftNode`: `NewConfig`, `Start`, `Config` (`peers`, `tickMs`, `electionTicks`, `compactEvery`, `seed`,
`actor`), messages `Tick`, `Propose`, `Query`, `Reply` (`ok`, `notLeader`, `lost`), `Caps`.
`RaftJournal`: `NewMachine`, `New(net, caps, n, id)` → `Client` (`tickMs`, `timeoutMs`, `giveUpMs`,
`timeoutTicks`, `giveUpTicks`, `Close`), `Tick(c)`, mutations `mutNoWait`, `mutNoExpected`.
`RaftSm.StateMachine.Query` is the read for `RaftNode.Query`.
