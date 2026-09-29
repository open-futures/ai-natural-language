# The Philosophy of the TTS SDK

This document is the *constitution* of the project. Every architectural
decision, every accepted or rejected feature, every line of code should
be defensible against the principles below. When in doubt, refer here.

> The format is older than its competitors in spirit, even though it's
> newer in code.

---

## 1. The file is the database

A single `.ttf` or `.attf` file *is* a database. It has a schema (the TFD
directory), indexed random access, ACID via WAL, versioning via
`next_tfd`, and integrity via SHA-256.

There is no server to run, no port to expose, no process to crash. You
`cp` it to back it up. You `scp` it to migrate. You `git add` it to
version it. You `cat` it to inspect it (TTF) or `od -c` it (ATTF).

This is the SQLite philosophy — *"the database is a file"* — pushed to
its logical conclusion for the agent-memory era.

---

## 2. Text is the substrate, not a serialization format

JSON, YAML, Protobuf, MessagePack — they all treat text as something you
*encode* data into. TTF treats text as what it already is: **the thing
itself**.

- In TTF, the data stream is plain UTF-8. Not encoded, not escaped, not
  base64'd. The bytes you store are the bytes you read.
- The directory describes the text; it doesn't *become* the text.
- A human can open a `.ttf` file in `vim`, edit a section, and the SDK
  will detect the tampering (hash mismatch) but still let them read
  everything else.

ATTF goes further: control characters are first-class bytes, not
JSON-string-escaped `\u0003`. The format was *designed backwards from
the tokenizer*, not forwards from a parser.

---

## 3. Zero-copy is a moral position

Zero allocations on the hot path of `get_section()` / `get_page()`. The
reader returns `std::string_view` into the mmap'd buffer. The writer
doesn't allocate during emission. The SHA-256 hash is computed on a
stack-resident `std::array`. The keyword index returns views, not copies.

> **Allocations are an admission of insufficient design.**

Every `new` / `malloc` / `std::string` copy in a parser is a place where
the designer said "I don't know the lifetime, so I'll copy." TTS says:
we *do* know the lifetime. The reader owns the bytes; the views live as
long as the reader. No ambiguity, no copy.

---

## 4. Backward compatibility is a contract, not a courtesy

TTF v2.0 files parse without modification in v3.0 and v3.5. v3.0
features (compression, encryption, escapes, embeddings) are all
*optional fields*. Old readers ignore them; new readers handle them.

This is a deliberate rejection of the "major version = breaking change"
model. TTS treats every existing file as a permanent obligation. If
Lotus Lab ships v4.0 in 2028, files written in 2026 must still open.

Adding a feature means claiming a new tag number or a new optional field,
not rewriting old ones. The format grows *orthogonally* — it doesn't
replace itself.

---

## 5. Integrity is the floor, not a feature

Every section in TTF is SHA-256 verified on read. Every page in ATTF is
implicitly typed and address-bound. The comparison is **constant-time**.

Most formats treat integrity as an add-on. TTF treats it as the floor:
if the hash doesn't match, you don't get the bytes.

> **Unverified data is not data. It is noise that looks like data.**

A document whose hash doesn't match might be a corrupted backup, a
malicious edit, or a cosmic-ray bit flip. TTF doesn't try to guess. It
refuses to lie about what it knows.

---

## 6. The C ABI is the API

Every public surface — Python, Rust, Go, Node.js, the C++ classes
themselves — is built on top of a small C ABI in `include/tts/ffi.h`.
The C++ classes are convenient; the C ABI is canonical.

> **A C ABI is the only universal language in computer science.**

Every language can call C. No language can call C++ directly. By making
the ABI the canonical surface, TTS ensures that Python, Rust, Go, Node,
Swift, Kotlin, Zig, and whatever language is invented next year can all
read the *same* `.attf` file with no extra work.

This is also why the SDK vendors its own SHA-256 and AES-GCM: pulling in
OpenSSL would break the C ABI's portability. **The dependency surface is
the API's portability surface.** Every external dependency added is a
language the SDK can no longer reach without a port.

---

## 7. Agents should think in addresses, not in documents

ATTF's signature feature is the `[Ref:0x5C11]` pattern. An agent doesn't
say *"according to my memory of OAuth2 tokens..."* — it says
*"[Ref:0x5C11] says tokens must expire after 900 seconds."*

This is a different model of agent cognition than what LangChain/MemGPT
promote:

- **MemGPT model:** the agent has a "memory" — an amorphous blob of
  context that grows and contracts. Memory is something the agent *has*.
- **ATTF model:** the agent has *addresses* — stable, content-addressed
  references to specific knowledge. Memory is something the agent
  *points to*.

This is closer to how a C program thinks about memory: you don't load
all of RAM into a register; you load a pointer and dereference it on
demand.

> **AI should think not by remembering everything, but by knowing where
> to look.**

---

## 8. Compression and encryption belong in the format, not the application

v3.0 added per-section zstd and AES-256-GCM. These are not features the
application has to wire up — they're fields in the SectionLayout.

> **If a format doesn't support compression and encryption natively,
> every file written is a security and storage regression.**

Most "modern" formats — JSON, YAML, even Parquet — leave compression
and encryption to the application. The result: applications don't do
it. Files bloat. Secrets leak. TTF makes these decisions at the format
layer so the application doesn't have to.

---

## 9. The source code is the standard

There's no separate ISO standard, no working group, no ratification
process. The spec is `docs/SPEC_v3.md`. The implementation matches it.
If the spec and implementation disagree, both are wrong.

This is the Unix philosophy: *the source code is the documentation*.
TTS extends it: *the source code is the standard*.

Conformance is verified by tests, not by committee.

---

## 10. The right answer is usually "no feature"

For every feature in TTS, there are three we rejected:

- **No server mode.** The value proposition is "no server." Adding a
  server would make it a worse ChromaDB.
- **No SQL interface.** TTF is not relational. SQL would invite
  comparisons SQLite wins.
- **No proprietary embedding model.** TTS wraps `IEmbedder` and lets you
  plug in OpenAI, BGE, or your own. Training models is a distraction.
- **No blockchain.** On-chain TTS storage is orthogonal. The format
  works the same on a Commodore 64 as on Ethereum.
- **No SaaS.** OSS + permissive license + self-hosted. A SaaS would
  alienate the audience that values data sovereignty.
- **No libsodium / OpenSSL as a hard dependency.** Vendored crypto keeps
  the dependency surface at zero. AES-NI intrinsics are allowed because
  they're CPU features, not external dependencies.

The unifying principle:

> **Refuse features that compete with the format's identity.**

TTS's identity is *file-based, integrity-checked, token-efficient,
polyglot*. Anything that dilutes one of those four adjectives is
rejected, even if it would add users.

---

## 11. Determinism is a feature

`TtfWriter::build()` is deterministic: same input → byte-identical
output. The streaming writer is deterministic. The hash embedder is
deterministic. The linter is deterministic.

> **Non-deterministic file formats are untestable.**

If a writer could produce different bytes for the same input (due to
hash-map iteration order, time-of-day floats, pointer addresses), then
files wouldn't diff cleanly in git, reproducible builds would be
impossible, and two readers opening the "same" file might disagree.

TTS makes determinism a hard constraint. The cost: more careful design
(ordered maps, explicit sort). The benefit: every file is a build
artifact, every byte accountable.

---

## 12. The format is anti-monopoly

The TTS spec is owned by Lotus Lab but licensed "free for all forever."
The implementation is open source. The format itself is designed so
that *anyone* can implement it in any language without permission.

This is a deliberate rejection of the format-as-moat strategy that
dominates proprietary AI infrastructure:

- OpenAI's function-calling format only works with OpenAI
- Anthropic's MCP, while open, is designed for Anthropic's agent model
- Pinecone's index format is proprietary

TTS's bet:

> **The winning agent-memory format will be the one that's most
> portable, not the one tied to the most powerful model.**

Models change every quarter. Formats last decades. Betting on a
portable format is betting on time.

---

## How to use this document

When proposing a new feature, ask:

1. Does it dilute any of the four identity adjectives
   (file-based, integrity-checked, token-efficient, polyglot)?
2. Does it add a hard dependency that breaks portability?
3. Is it something the *format* should do, or something the
   *application* should do?
4. Is it deterministic?
5. Does it preserve backward compatibility?

If the answer to any of (1), (2), (4), or (5) is "yes," the feature is
rejected. If the answer to (3) is "application," the feature is
rejected from the SDK core but may live as an opt-in adapter.

This document is append-only. New principles can be added; existing
ones cannot be removed or weakened.
