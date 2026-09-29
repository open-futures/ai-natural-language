# Text Tagged Specification (TTS): A Better Way to Store Structured Text

**Version:** 2.0  
**Standard Specification**

## 1. What is TTS and Why Does It Matter?

**Text Tagged Specification (TTS)** is a plain-text file format that borrows smart ideas from the classic **TIFF** image format — but for text. Instead of mixing structure and content together (like most Markdown or JSON files), it keeps a clean directory of metadata at the front and the actual content in a separate data stream.

This gives you:
- Random access to any section without reading the whole file
- Strong integrity checking (hashes)
- No more broken boundaries when someone writes "---" or other delimiters inside the text
- Two flavors: one friendly for humans, one super-efficient for AI agents

**TTF** – Best for humans, archives, and regular software.  
**ATTF** – Best for LLMs, agents, and memory systems (extremely low token usage).

---

## 2. TTF – The Human-Friendly Version

A TTF file has four clear parts in order:

1. **Magic Header** – Tells you the version and type.
2. **Text File Directory (TFD)** – Metadata that describes every section (offsets, lengths, hashes).
3. **Data Stream** – The actual content, stored cleanly.
4. **EOF Marker** – Clean ending.

### Example

```text
[TTF_LE:2.0]
---
tfd:
  tfd_id: 1
  next_tfd: 0
  encoding: "UTF-8"
tags:
  - id: 256
    name: "DocumentTitle"
    value: "System Architecture Dossier"
  - id: 273
    name: "SectionLayouts"
    value:
      - section: 1
        offset_chars: 482
        length_chars: 185
        sha256: "..."
      - section: 2
        offset_chars: 667
        length_chars: 241
        sha256: "..."
---
[DATA_STREAM_START]
This is the actual content of section 1. The parser doesn't scan for delimiters — it just jumps to the exact character position and takes exactly the right number of characters. You can safely put --- or any other markers inside here.

This is section 2. If someone manually edits the text and changes the length, the SHA-256 hash will no longer match and the system will know something's wrong.
[TTF_EOF]
```

**Key Benefits:**
- The reader loads the directory first, then jumps directly to any section using character offsets.
- Editing the content automatically invalidates the checksum — great protection against accidental (or malicious) changes.

---

## 3. ATTF – The Agent & LLM Optimized Version

ATTF strips away almost everything that wastes tokens. It uses single-byte control characters (which cost only 1 token in most modern tokenizers) and a memory-page style addressing system.

### Control Characters

| Symbol | Hex   | Meaning                          | Token Cost |
|--------|-------|----------------------------------|------------|
| ␁      | \x01  | Start of heading / variant       | 1          |
| ␄      | \x04  | Global settings                  | 1          |
| ␃      | \x03  | End of entry / row delimiter     | 1          |
| ␂      | \x02  | Start of data stream             | 1          |

### Example ATTF Memory File

```text
␁ATTF_MEM:2.0
␄mem_zone:active|last_sync:2026-07-11
␃400:1,sys_prompt,0x00A1,9.9
␃400:2,episodic,0x0BF2,8.5
␃401:0x5C11,["kernel", "optimization", "debian"]
␂
␃P1:0x00A1
You are a Kernel Optimization Agent...
␃P2:0x0BF2
[2026-07-11 14:00] User requested a TIFF-to-text alternative...
␃
```

This format lets agents quickly scan the index, decide which memory pages to load, and ignore everything else — saving huge amounts of context window.

---

## 4. How to Use It

### Parsing a TTF File (Improved Python Example)

```python
import hashlib

def parse_ttf(file_path: str):
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Find header and TFD
    if not content.startswith("[TTF_LE:2.0]"):
        raise ValueError("Invalid TTF header")

    tfd_end = content.find("---", content.find("[TTF_LE:2.0]"))
    # Better parsing logic would go here (e.g. using regex or a proper parser)

    stream_start = content.find("[DATA_STREAM_START]\n") + len("[DATA_STREAM_START]\n")

    # Extract sections using directory...
    # (Full robust implementation would validate all offsets and hashes)
```

**Note:** The original parser works as a starting point but should be made more robust (avoid fragile string slicing for YAML extraction).

### Adding New Memory to an ATTF File (Agent Perspective)
1. Calculate the new entry (`␃P{NEW_ID}:address\n[content]\n`)
2. Append it before the final boundary
3. Update the memory directory at the top

---

## 5. Quick Examples

**Simple TTF Document**

```text
[TTF_LE:2.0]
---
tfd: ...
---
[DATA_STREAM_START]
Fixed kernel allocation buffer overflow bugs.
[TTF_EOF]
```

**ATTF Semantic Memory Chunk**

```text
␁ATTF_MEM:2.0
␄mem_zone:long_term
␃400:1,semantic,0x1111,9.5
␃401:0x1111,["auth", "oauth2"]
␂
␃P1:0x1111
OAuth2 tokens must expire after 900 seconds...
␃
```

**Agent Response Style (using references)**  
"According to loaded security policies [Ref:0x1111], connections ignoring renewal protocols should be terminated immediately."

---

> Made by Lotus-Lab -> https://lotuschain.org -> free for all for ever, just credit us for ever :)
