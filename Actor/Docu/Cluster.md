# A Cluster

An exchange keeps accounts and moves money between them. There are few accounts and many transfers,
and nodes fail: one crashes, another disappears from the network for a while. The application needs
three properties. The sum of all accounts does not change. A confirmed transfer is not lost. A
repeated transfer does not move money a second time. A local actor with a journal does this on one
process; here the same actors work on several processes and machines.

## 1. What the cluster is made of

A cluster is several `blackbox-console` processes, and each holds three `ActorNet` nodes. Node `G<i>`
is the virtual actors, the client and the host of the grains. Node `J<i>` is a member of the Raft
cluster with the journal. Node `M<i>` is a member of the Raft cluster of the membership service. The
names and addresses lie in the file `nodes`; `G`, `J` and `M` with the same number belong to one
process, but are three different addresses.

The journal is the `Raft` subsystem: five voters, a majority of three. The membership service is
also five voters. Five processes survive the loss of two: the remaining three keep a majority in
both clusters. Accounts live in the journal as the streams `acct.<number>`, transfer coordinators as
`xfer.<number>`.

An account is an `ActorGrain` actor of the kind `"acct"`. Its address is the account number, not a
node. The owner of a key is the live member with the greatest weight `hash(member, kind, key)`; when
a member leaves, only its keys move. A letter to a key's address goes to the owner's host; the host
brings the activation up and passes the letter on. An activation is a persistent actor: the state is
read from the journal stream, so it comes up on any node.

The membership service `RaftMembership` keeps the list of participants. A `RaftGrainMembership` member
joins it with its capability, renews the lease and reads the list from time to time; every list is
passed to `ActorGrain.ApplyMembers`. A new participant is added, a returning one is alive again, a
participant absent from the list is dead. A lease expires when a node stops renewing it: for the
others it is lost in the same way as a node whose TCP connection broke.

## 2. A transfer is a saga

A transfer has three steps: debit the source, credit the destination, and, if the credit is refused,
return the debit. The states of the coordinator are `begun`, `withdrawn`, `done`, `refunding`,
`refunded`, `failed`. A step is a command to an account with the number `transfer * 3 + step`: 0 is
the debit, 1 the credit, 2 the refund. The account remembers the operation numbers; a repeat with
the same number returns the recorded result and does not move the balance.

The coordinator is also a virtual actor, of the kind `"xfer"` with the transfer number as its key.
In its journal it keeps only the current step. It holds no account addresses: it hands the step to
the client as a `Cmd`, the client executes the `Move` and answers `Done`. Thus the coordinator does
not depend on the node it came up on, and the client of a node holds the account addresses.

```
coordinator: Persist(Step) ──> Persisted ──> Cmd  ──> client
client:      Move ──> account ──> Reply ──> Done ──> coordinator
```

If the node with the coordinator crashes, the `Cmd` is lost. The client repeats `Begin` and `Tick`;
the coordinator comes up on another node, reads the step from the journal and sends the `Cmd` again.
The command on the account is idempotent, so the repeat is safe. If a client crashes, a client of
another node continues its work: `Begin` is idempotent, and `Tick` makes the coordinator send the
current step.

A credit is refused when the balance of the destination would exceed `limit`; a debit when the
balance of the source would fall below zero. A refused credit moves the saga to `refunding`, and a
refund always passes. Money therefore never stays debited without being credited.

Refused transfers do not disappear from the report: the client waits for the final state of every
transfer and counts `done`, `failed` and `refunded` separately. The sum of these numbers equals the
number of transfers.

## 3. Starting

Build the installation and run the scenario:

```sh
tools/build64.sh --no-help
tools/iso.sh tools/test-cluster.sh --only obxcluster --runs 1
```

The scenario compiles `ObxCluster` and `Actor_devCluster`, creates the run directory and starts five
processes with the external variables `--SCENARIO=obxcluster --IDX=<number> --N=5 --TICK=20
--OPS=<transfers>`. The scenario hands out addresses and ports; the file `nodes` in the run
directory looks like this:

```
G0 127.0.0.1:20001
J0 127.0.0.1:20002
M0 127.0.0.1:20003
...
```

Every process reads the addresses from `nodes`, brings up three nodes, writes the capabilities
`J<i>.cap` and `M<i>.cap` and waits for the capabilities of the neighbours. Then it creates the
journal client `RaftJournal.New`, the membership client `RaftMembership.New`, the cluster
`ActorGrain.New`, registers the kinds `"acct"` and `"xfer"`, and joins the membership service through
`RaftGrainMembership`.

The work starts by the file `start`: the client sends no `Begin` until the file exists. The counter
of process starts lies in `<node>.inc`; a restarted process calls `ActorNet.SetIncarnation` with the
next number, otherwise the neighbours drop the frames of the new incarnation as duplicates. The
secret of a node comes from its name (`SecretOf` in the scenario), so the marks of the capabilities
survive a restart.

The scenario `obxcluster` in steps:

1. Waits for the readiness of five processes and for a list of five participants.
2. Stops process `P3` with `SIGSTOP`: this is a fake network split, the node does not answer and does
   not renew its lease, but it is not killed.
3. Writes `start`; four processes begin the transfers.
4. Kills two processes with `kill -9`, preferably the leaders of the journal and of the membership
   service.
5. After two seconds brings `P3` back from the split and starts the killed processes again, with the
   same storage.
6. Waits for the result of every process and compares the sum, the absence of repeats and the count of
   refusals.

## 4. Adding and removing a node

The cluster of the example is fixed: five processes. A new node is added like this.

1. Choose a free number `id`; it is also the index in the arrays `RaftNode.Caps`, so the number must
   be the same on every node.
2. Start a process on the new machine with the addresses in the shared file `nodes`; its nodes
   `J<id>` and `M<id>` come up and publish their capabilities.
3. On the running nodes of the Raft cluster call `RaftNode.SetPeer` with the capability of the new
   node, then `RaftNode.AddServer`. The leader enters the node as a learner; when the learner has
   caught up with the leader, the leader appends the record that turns it into a voter. The new node
   receives the journal and votes only after that.
4. For the membership service the new process itself calls `Join` with its capability through
   `RaftGrainMembership.Start`; the participant number is `id`. The membership record appears in the
   list, and `ActorGrain.ApplyMembers` adds the participant on every node.
5. For the account journal the new participant must also become a voter: without this the journal
   stays on the former five, and the new node works only as the owner of keys.

Removing a node is the reverse order. The member of the membership service calls
`RaftGrainMembership.Leave`; the record stays in the list with `alive = FALSE`, and the keys move to
the live members. Then `RaftNode.RemoveServer` on both Raft clusters removes the voter. The storage
of the removed node is deleted when it is no longer needed: a node that returns with the same number
and the same storage joins the service again; for the Raft clusters it needs `AddServer` once more.

The lease is the second way out. A node that stopped renewing its lease is dead for the others but
stays in the list; its keys move. When it returns and renews the lease, the list marks it alive
again, and the keys may return. An explicit `Leave` differs in that the node no longer joins and
does not renew the lease itself.

`RaftGrainMembership.NewConfig` sets the lease parameters: `lease` is a term in milliseconds,
`renewEvery` and `refreshEvery` are in ticks. In the example the lease is 4000 ms, renewal and list
reading happen once in ten ticks; at a tick of 20 ms this is 200 ms. Too short a lease produces false
deaths under delays, too long a lease delays the movement of keys.

## 5. What is guaranteed

**One writing copy of a key.** The journal `RaftJournal` checks the expected entry number when it
applies a command. If two copies of an activation try to append to the stream, the second receives
`conflict`; a persistent actor traps on a conflict and stops, and the next letter brings it up again
and makes it catch up. A read before every letter goes through `SubmitLast` and `ReadIndex`: a copy
cut off from the majority does not answer at all instead of returning stale state.

**What a majority confirmed is not lost.** `RaftNode` answers `ok` to a client only after the entry
is committed by a majority and applied on the leader. The loss of the leader after the answer does
not cancel the entry: a majority holds it.

**A repeat does not move money a second time.** The operation number in an account is part of the
state: the account keeps the list of applied numbers and answers a repeat with the recorded result.
This holds after a restart as well: the list is restored from the journal together with the balance.

**The sum is conserved.** A debit and a credit are two entries of one transfer; a refund cancels the
debit when the credit was refused. An unfinished transfer is either not started or returns to its
initial state; the report waits for the final states.

**Refusals are counted.** The client counts `done`, `failed` and `refunded`; their sum equals the
number of transfers. The scenario compares this number, the sum of the balances, the number of
events `2*done + failed + 3*refunded` and the absence of repeated events (`dups = 0`) on every node.

## 6. What is not there

- The channel of the nodes is neither encrypted nor authenticating the sender: the frames are
  plaintext, and the "from" field is protected only by the capability mark. Channel protection is
  section 5 of the plan [docs/cluster-plan.md](../../docs/cluster-plan.md); the open keys of the
  participants are stored in the membership record but are not checked.
- Delivery is at most once. The transport does not repeat a lost frame; the application does: the
  client sends `Tick`, the coordinator sends the step again, the account recognises the operation
  number. An application that makes no repeats simply loses a letter when a frame is lost.
- The client does not know about a change of leader in advance: `RaftJournal` and `RaftMembership`
  move to the named leader and repeat the request themselves, but this is visible as a delay, not as
  an immediate refusal.
- The membership service does not delete the records of removed and crashed participants; the list
  grows up to `maxMembers`.
- `ActorGrain` does not coordinate placement itself: on a network split two copies of a key are
  possible. Only the copy that sees a majority of the journal writes; the second receives `conflict`
  or no answer. This is a property of `RaftJournal`, not of `ActorGrain`.
- The start counter and the storage of the nodes are files of the run directory. A shared directory
  on two machines must be truly shared (or synchronised): otherwise a restart takes an old
  incarnation number and old capabilities.
- An account remembers at most 128 operation numbers (`maxOps`). For the example's 24 transfers this
  has room to spare; a long stream needs a snapshot that prunes the list or another memory of the
  numbers.

## 7. Two machines

The same cluster can be spread over two machines. Addresses in `nodes` are written with the
addresses of the machines, not with `127.0.0.1`:

```
G0 10.0.0.1:20001
J0 10.0.0.1:20002
M0 10.0.0.1:20003
G3 10.0.0.2:20010
J3 10.0.0.2:20011
M3 10.0.0.2:20012
...
```

The steps:

1. Build the same installation on both machines (`tools/build64.sh --no-help`).
2. Put the run directory on a common path visible to both machines: the files `nodes`, `*.inc` and
   `*.cap` are read by every process, while `J<i>.kv` and `M<i>.kv` are written only by their own
   node. Copies of the directory do not work: the start counters and the capabilities would diverge,
   and two nodes would get one storage.
3. On each machine start its processes with the common `--DIR`, its own `--ROLE=P<i>` and
   `--IDX=<i>`. Ports within a machine must not collide; the addresses in `nodes` are the addresses
   of the machines.
4. Make sure the machines see each other's ports and wait for the readiness files. The rest the
   scenario does as on one machine: `start`, kills, restarts.
5. A check without a second machine: `tools/iso.sh tools/test-cluster.sh --only obxcluster --veth`.
   Two network namespaces are joined by a veth pair; the addresses are `10.10.0.1` and `10.10.0.2`.

## 8. Reference

The module `ObxCluster` is the application of the example. Its exports:

- `accounts = 8`, `initial = 1000`, `limit = 1300` — accounts, initial balance and balance limit.
- `maxTransfers = 64` — the limit on the number of transfers of a client.
- `begun = 0`, `withdrawn = 1`, `done = 2`, `refunding = 3`, `refunded = 4`, `failed = 5` — the
  states of the coordinator.
- `Plan(id, from, to, sum)` — the source account, the destination account and the sum of transfer
  `id`. Post: `0 <= from, to < accounts`, `from # to`.
- `SetCluster(cl)` — the cluster in which the client addresses accounts. Pre: `cl # NIL` — 20.
- `NewAccount()`, `NewCoord()` — factories for `ActorGrain.Register`.
- `SpawnClient(parent, name, n, pace, sequential, verbose, sink)` — the client of a node. Pre:
  `0 <= n <= maxTransfers` — 20, `sink # NIL` — 21. Post: the address of the client; `pace > 0` is
  at most `pace` new `Begin` per tick, `sequential` drives transfers one at a time, `verbose` prints
  the outcome of every transfer, `sink` receives the report `Report`.
- `Progress()` — how many transfers of the client of the node are finished.
  `Status(drives, n, finished, reporting, reported)` — the same counters and the phase of the report.
- `Active()`, `ActiveState()` — the current transfer of a sequential client and its state.
- `Final(state)` — the state is final (`done`, `refunded`, `failed`).
- `Do()` — a short run in one process: two nodes, a shared journal in memory, the loss of a node.

The letters and events of the example (`Move`, `Begin`, `Cmd`, `Done`, `Step`, `Moved`) are in the
source on the page [Obx/Docu/Cluster.md](../../Obx/Docu/Cluster.md); the scenarios and checks are
in `tools/test-cluster.sh`.
