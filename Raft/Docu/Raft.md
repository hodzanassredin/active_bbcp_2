# Raft: a replicated log

## Why

An account lives in an event journal, and the journal lives on one machine. The machine dies and the
account is gone. Put the journal on three machines and write to all three. Now the network splits: two
machines see each other, the third sees nobody, and its client writes too. When the network heals, one
position of the journal has two different values. This is exactly the limit of the actors
(`ActorGrain`): under a partition a key gets two incarnations, and nothing tells which one is right.

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

### Elections under partitions

A node cut off from the leader times out, raises its term and asks for votes. When the network heals, that
higher term deposes a leader that was working all along, and the cluster loses a round of elections for
nothing. An old leader in a minority has the opposite problem: it believes it still leads until it hears a
higher term, though it can neither commit nor read.

PreVote (§9.6 of the dissertation) closes both. A node whose election timeout expired first asks for
*pre-votes* with the term it would use, one above its own, but does not adopt that term and does not vote
for itself in it. A server grants a pre-vote if the candidate's log is at least as up to date as its own
and it has not heard from a leader within an election timeout; a leader never grants. Only after a majority
of pre-votes does the candidate raise its term and hold a real election. A node returning from a partition
collects no pre-votes while the others hear the leader, so its term stays where it was and the leader
stays. A candidate that times out repeats the pre-vote instead of raising its term again.

CheckQuorum makes the old leader step down. The leader remembers the tick of the last answer from every
voter; on each tick it counts those heard within an election timeout, itself included. If the count is
below a majority, the leader becomes a follower in the same term. It stops sending heartbeats and its
pending reads are dropped; a higher term is not needed for this.

A node grants a vote by the freshness of the candidate's log, not by its own place in the configuration.
The candidate counts only voters of its effective configuration, so a vote from a learner or from a removed
node does not make a majority. The rule also lets a server vote before it has learned that it is a voter:
when the entry that promotes it is committed but the node has not yet heard the commit, it still thinks it
is a learner, and without the rule it would refuse the vote a majority needs.

### State machine and clients

Committed entries are applied to a state machine (`RaftSm.StateMachine`: `Apply(index, command)` →
result, `Snapshot`, `Restore`) on every node in the same order, so the copies agree.

A client that got no answer retries — perhaps with a new leader — while the first attempt may already be
in the log. `RaftSm.Sessions` prevents double application: a command carries a client number and an
operation number (`RaftSm.Encode`); the session remembers the client's last applied number and result and
answers a retry with the same result. A client sends one operation at a time.

Sessions do not live forever. A session also remembers the term of the last entry it applied. Before
applying an entry of term `t`, `Sessions` removes every session whose term is more than `expiry` terms
below `t` (`Sessions.SetExpiry`; 0 disables expiry). `RaftNode.Config.sessionExpiry` sets the term for the
driver and is 1000 by default. The rule is the one of §6.3 of the dissertation with the term as the clock:
the cluster cannot tell whether a silent client is gone, so it forgets it after a long silence. A client
that returns after its session was removed may have an old operation applied a second time; a new client
incarnation uses a new client number. The session snapshot stores the term, so a restored node evicts by
the same rule as the others.

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

### Changing the cluster

The composition is a set of voters and learners stored in the log. A change is an entry of kind
`entryAddLearner`, `entryAddVoter` or `entryRemove` (`RaftCore.AddServer`, `RemoveServer`); the node
number is in the entry. One server changes at a time, and the leader starts a change only when the
previous one is committed and it has committed an entry of its own term. The second rule is the fix of the
2015 bug in single-server changes: without it, a leader that inherited an uncommitted configuration could
append another one, and two configurations with disjoint majorities could commit different entries across
a change of term.

A new server enters as a learner. Learners receive `AppendEntries` and snapshots but do not vote and do
not count in a majority. When the learner has caught up to the leader's `commit`, the leader appends
`entryAddVoter`; the learner becomes a voter when that entry commits. A server that is far behind catches
up by snapshot first; a voter added without catch-up would make a majority harder to reach and could be
elected with a log that misses committed entries.

A configuration entry takes effect when it is committed by a majority of the configuration in force, not
when it is appended. The dissertation applies a configuration as soon as it is in the log; that leaves a
window in which a leader that crashes after appending a change leaves one server removed from its own
configuration, or a new voter that does not yet know it is a voter, and the cluster has no quorum. Ongaro
names the committed variant safer; etcd uses it. The 2015 barrier is kept with it. When the removal entry
of the leader commits, the leader steps down; until then it is still a voter and still sends the entry to
the server being removed.

The effective configuration is `voters` and `learners`; a majority is a majority of `voters`. A server
that has an uncommitted configuration entry keeps the old configuration: the entry is pending, and
`confDirty` blocks the next change. The term, vote, log and the known commit index survive a restart.
The commit index never decreases, and a node that forgot it would fall back to a configuration older than
its log — two such nodes could then form a majority of the old configuration and elect a leader without
committed entries. A snapshot stores the configuration at its index; after a restart the effective
configuration is rebuilt from the snapshot and the log up to the restored commit.

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

`RaftNode.Status` gives the owner what an outside driver needs: the role, term, leader hint, the last
index and its term, the commit and applied indexes, and the storage-failure flag. The driver of a
process reads it to follow an election. A cluster of three or five `blackbox-console` processes on
`ActorNetTcp` is the scenario `tools/test-cluster.sh`: elections, writes, `kill -9` of the leader,
re-election, restart of the node with the same `RaftKvStorage`, agreement of the logs. A separate
client process sends `Propose` one operation at a time; the script stops it after a fixed number of
confirmed commands, kills the leader, and the client continues from the new leader with the same
operation number. Over loopback TCP with storage in memory the client confirms about 930 commands per
second; the numbers are in `docs/measurements.md`.

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

### Membership service

A cluster needs one list of who is in it. A node that joins must be found by the others, a node that
leaves or dies must lose its keys, and a node that returns must be taken back. The list also carries
the address of each node and its open key, so a node does not need every other address in advance.

`RaftMembership` keeps that list in a separate Raft cluster of three or five nodes. Its state machine
holds one record per member: the node number, the capability of the member's grain host
(`ActorNet.Export`), the open key, the moment of joining and the end of the lease. Join, Leave and
Renew are log commands; List is a query and therefore goes through `RaftNode.Query`, that is, through
ReadIndex: a read sees a state that a majority has confirmed. The machine is deterministic: the time
`now` comes in the command and in the query rather than from a clock, so replicas apply the same bytes
and answer with the same list. All participants use one clock: in processes it is `Services.Ticks`, in
the model the driver's counter (`RaftGrainMembership.time`).

Liveness is a lease. A member is alive while `leaseUntil > now`; each participant renews its lease
every few ticks. A node that stops renewing becomes dead for the others after the lease, and its keys
are placed among the living. A snapshot carries the records, the active flags and the leases; a
restarted service node restores them.

`RaftMembership.Client` is the actor-side agent. It sends `Propose` and `Query` to the node it believes
is the leader, follows the leader hint, repeats on a timeout and lowers the answer into a `Result` in
the requester's mailbox. One client executes one operation at a time, so the Raft session makes a
repeated command idempotent.

`RaftGrainMembership` is the bridge to `ActorGrain`. It joins with the host's capability, renews the
lease, reads the list and calls `ActorGrain.ApplyMembers`; that call adds new members, revives the
returning ones and marks those absent from the list dead. Placement of keys is computed from the live
members on every send, so it changes with the list. The key is stored and handed to `ActorGrain`;
signature and certificate checks are not done.

The node driver accepts the configuration commands: `SetPeer` writes the capability of a new node,
`AddServer` and `RemoveServer` post commands to the core (only the leader acts, the others ignore the
letter), and `ConfStatus` reports the effective voters and learners. A new node starts as a learner,
catches up and becomes a voter after the leader sees its `match` reach `commit`.

`Raft_devMembership` checks the machine directly, the client against three service nodes (join, list,
`kill -9` of the leader, renew, leave), the driver's configuration commands, four state-machine mutations and three mutations of the driver's configuration path.
`Raft_devGrain` runs two grains whose members come from the service: a key moves on leave and on node
loss, and twelve seeds cover a partition of the service minority, a lost owner and a leaving owner; each
operation is applied once. The process scenario `tools/test-cluster.sh --only membership` starts five
service processes, brings two of them into the voters, starts a fourth participant, removes and kills a member, kills the
service leader and checks that deposits are applied once.

## Testing

`Raft_devSim` is a deterministic simulation of 3 and 5 nodes. Messages are lost, duplicated and
reordered (within a window of the eight oldest), nodes crash — in the middle of a `Ready` between saving
and sending, and in Kv mode in the middle of a commit with a `KvFault` power cut; the network splits into
islands (minority and majority) and in one direction; node clocks run at different rates; counter
clients retry and read. PreVote and CheckQuorum are on. After every step the test checks:

- at most one leader per term;
- log matching: the same term at index i on two nodes means the same entries 1 .. i;
- leader completeness: a new leader holds all committed entries;
- what is committed and applied at index i is the same entry on all nodes and never changes;
- a committed entry is not replaced in a node's log: the entry under the highest decided index up to
  `commit` is the decided one, and a message that shortens the log below `commit` is a violation;
- at the end, with the network healed, the cluster commits again;
- the client history is linearizable (a counter: increment returns the new value, read the current one).

The membership mode (`RunM`) starts 5 nodes with 3 voters, or 7 with 5, and changes the composition while
the faults run: a leader adds a node (learner, catch-up, voter), removes a node, or removes itself. The
effective configuration is checked through the log: every committed entry is decided once, and a node
that is not in its own configuration does not campaign.

The regular run is 500 seeds on 3 nodes and 500 on 5, 200 seeds with membership on 5 nodes (3 voters),
60 with membership on 7 nodes (5 voters), 40 + 20 seeds on Kv and 20 with membership on Kv, 2000 steps
each (1500 on Kv; about 10 s). The long ones are `Raft_devSim.Long` (3000 + 3000 seeds of 3000 steps) and
`Raft_devSim.LongMembership` (1000 + 300 seeds of 3000 steps with membership). A failure prints the seed;
a step-for-step replay, from the install directory (`tools/run64.sh` passes no parameters):

```
echo 'Raft_devSim.Replay' | ./blackbox-console --RAFT_SEED=<seed> --RAFT_N=5 --RAFT_STEPS=3000
echo 'Raft_devSim.ReplayM' | ./blackbox-console --RAFT_SEED=<seed> --RAFT_N=5 --RAFT_INIT=3 --RAFT_STEPS=3000
```

Twenty-two mutations of the algorithm are switched on by `RaftCore.mutations` (tests only) and must be
caught. The original eight: a vote without the up-to-date check, committing an old-term entry by counting
replicas, answering before saving, keeping a conflicting tail, two votes in a term, applying uncommitted
entries, reading without confirming leadership, retrying without a session. The new ones: elections
without PreVote, a pre-vote granted against a heard leader, a pre-vote term adopted as one's own; a
leader in a minority that does not step down, CheckQuorum that counts any past answer, CheckQuorum that
demands every voter; a configuration change without the current-term barrier, a voter added without
catch-up, a removed leader that keeps leading, a removal that changes nothing, a commit forgotten on
restart; sessions that never expire, a session of the current term expired, and a session term not
updated. Two old mutations (two votes in a term, a stale read) are checked in a run with PreVote off:
with PreVote they do not show.

The scenarios isolate the mechanisms on hand-delivered messages: `TestPreVoteKeepsLeader` (a node returns
from a partition and does not depose the leader), `TestCheckQuorumStepsDown` and
`TestCheckQuorumMajority`, `TestConfBarrier` (a leader refuses a change until it commits an entry of its
term), `TestLearnerCatchUp`, `TestRemoveLeaderStepsDown`, and the session tests (`TestSessionsExpire`,
`TestSessionsRetry`, `TestSessionTermUpdated`, `TestSessionsSnapshotTerm`). Each has a mutation twin
that must fail: `TestMutNoPreVote`, `TestMutPreVoteIgnoreLeader`, `TestMutPreVoteAdoptTerm`,
`TestMutNoCheckQuorum`, `TestMutCheckQuorumEver`, `TestMutCheckQuorumStrict`, `TestMutNoConfBarrier`,
`TestMutNoLearner`, `TestMutNoStepDownRemoved`, `TestMutRemoveNoEffect`, `TestMutNoCommitRestore`,
`TestMutNoSessionExpire`, `TestMutExpireCurrent`, `TestMutSessionTermStale`.

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
`RaftJournal.mutNoExpected` on seed 8.

`Raft_devJournal` tests `RaftJournal` on the `ActorRun` pool in real time: a persistent counter writes and
recovers from the Raft journal, the leader's death loses nothing confirmed, and a leader cut off from the
majority does not confirm a write (Append returns through the new leader once the entry is on a
majority). The mutation `RaftJournal.mutNoWait` (answering "written" without waiting) is caught by this
scenario.

`tools/test-cluster.sh` runs the cluster in separate processes: three and five nodes on `ActorNetTcp`
inside one network namespace, addresses and capabilities distributed by files of the run directory.
The script checks that the client's confirmed commands are applied (the state machine returns their
count), kills the leader, measures the time until the next leader writes its status, restarts the node
with the same storage, and waits until every node reports the same last index, term and commit index.
The same scenario runs with two network namespaces joined by a veth pair (`--veth`), which stands for
two machines.

## Cost

`Raft_devBench.Run` was measured on 26.11.2026: 3 nodes in one process, a FIFO network without faults,
amd64 Linux, one OS thread; PreVote and CheckQuorum are on, and other test stands also ran on the
machine. Build and run:

```sh
tools/go64.sh System_devCRaw System_devClock RaftCore RaftSm RaftStorage RaftKvStorage KvPages KvStore KvFault KvLin KvTree KvDb Raft_devBench
echo 'Raft_devBench.Run' | tools/run64.sh
```

Three runs gave 0.41–0.52 million commands per
second in memory with one client, 0.46–0.68 with 8 and 0.45–0.47 with 64; on Kv with fsync 186–204
commands per second with one client (each waits for three commits of two fsyncs), 1.4–1.6 thousand with
8 and 8.9–10.9 thousand with 64, because 64 commands go in one commit per node (0.047 saves per command
against 3). An election after a leader crash takes 19 ticks on average, at most 34 (timeout 10..19; the
pre-vote round is included); with a 10 ms tick that is about 190 ms (derived). A node
100 000 commands behind catches up by a snapshot in 2.4–2.7 ms in memory. Membership changes were not
measured.

## What is missing

- The `RaftNode` actor runs on `ActorNetTcp` between processes in `tools/test-cluster.sh` (elections,
  `kill -9` of the leader, restart with the same storage, addition and removal of servers, `kill -9`
  of a member and of the leader). A process run does not partition the network; partitions are checked
  in the model.
- The `RaftJournal` journal under `ActorSim` is tested only on `ActorNetMem`; `ActorGrain` is tested on
  it only in the process scenario of `tools/test-cluster.sh`, without partitions. The journal state
  machine's snapshot is all accepted commands in a row; there is no separate truncation for it.
- The driver accepts membership commands and peer capabilities. `Config.n` is the initial number of
  voters; peers up to `RaftCore.maxNodes` can be given. The list of application members with addresses
  and open keys is a state machine of a separate Raft cluster, not Raft's own configuration. The key is
  stored but not checked; signatures, certificates and the protected channel are not checked.
- No joint consensus: only one server changes at a time, and a change is not batched with another.
- A configuration entry takes effect when it is committed, not when it is appended as in the
  dissertation. The 2015 barrier is kept. The model shows no loss with this order; the exact choice is
  in ADR 0032.
- Session expiry counts Raft terms, not wall-clock time. `RaftNode.Config.sessionExpiry` is 1000 terms by
  default; a client that returns after its session expired may have an old operation applied twice
  (§6.3 accepts this risk).
- The cost of elections and writes was measured on a fixed cluster; membership changes were not.

## Interface

`RaftCore`: `New`, `Tick`, `Step`, `Propose`, `ReadIndex`, `HasReady`, `GetReady`, `Advance`, `Compact`,
`SetBatch`, `TermAt`, `EntryAt`, `SetConfig`, `SetCommit`, `SetFeatures`, `AddServer`, `RemoveServer`,
`CountVoters`, `IsVoter`, `IsLearner`; roles `follower`, `preCandidate`, `candidate`, `leader`; message
kinds `prevoteReq`, `prevoteResp`; entry kinds `entryAddLearner`, `entryAddVoter`, `entryRemove`; node
fields `id`, `n`, `term`, `vote`, `role`, `leader`, `last`, `commit`, `applied`, `snapIndex`, `snapTerm`,
`voters`, `learners`, `snapVoters`, `snapLearners`, `confIndex`, `confDirty`, `preVote`, `checkQuorum`.
`RaftSm`: `StateMachine` (`Apply`, `Snapshot`, `Restore`), `Sessions` (`Apply`, `Snapshot`, `Restore`,
`SetExpiry`, `Count`, `Has`), `New`, `Encode`, `Decode`.
`RaftStorage`: `Storage` (`Save`, `Compact`, `Load`), `NewMem`. `RaftKvStorage`: `New(db)`.
`RaftNode`: `NewConfig`, `Start`, `Config` (`ref`, `peers`, `tickMs`, `electionTicks`,
`sessionExpiry`, `compactEvery`, `seed`, `actor`), `Status`, `SetPeer`, `AddServer`, `RemoveServer`,
`ConfStatus`, messages `Tick`, `Conf`, `Propose`, `Query`, `Reply` (`ok`, `notLeader`, `lost`), `Caps`.
`RaftMembership`: `NewMachine`, `Machine` (`Apply`, `Snapshot`, `Restore`, `Query`), `JoinCmd`,
`LeaveCmd`, `RenewCmd`, `ListQuery`, `DecodeList`, `Member`, `Members`, `New(net, caps, n, id)` →
`Client` (`Submit`, `Tick`, `SetPeer`, `Close`, `Stats`, `Trace`), mutations `mutNoLeave`,
`mutNoExpire`, `mutNoJoin`, `mutNoRenew`.
`RaftGrainMembership`: `NewConfig`, `Start`, `Tick`, `Leave`, `Status`, `Errors`, `time`, mutations
`mutIgnoreList`, `mutAllAlive`, `mutNoJoin`, `mutNoRenew`.
`RaftJournal`: `NewMachine`, `New(net, caps, n, id)` → `Client` (`tickMs`, `timeoutMs`, `giveUpMs`,
`timeoutTicks`, `giveUpTicks`, `Close`), `Tick(c)`, mutations `mutNoWait`, `mutNoExpected`.
`RaftSm.StateMachine.Query` is the read for `RaftNode.Query`.

`RaftNode.Status(cfg; OUT role, term, leader, last, lastTerm, commit, applied: INTEGER; OUT broken: BOOLEAN)`
reports the role (`RaftCore.follower`, `candidate`, `leader`; `none` before the storage is read), the
term, the leader hint, the last index and its term, the commit and applied indexes and the
storage-failure flag. It changes nothing; a driver calls it between switch points.
