# Logical Search Matrix: an operator-free stream filter

> **Status:** design proposal for Plan2001 userland, not a description of an implemented command. No kernel or native ABI changes are proposed.

## Purpose

A logical search matrix is a visual query language for filtering arbitrary input data, such as text received from standard input. The user enters **search terms in cells** instead of typing Boolean operators, parentheses, regular expressions, shell pipelines, or a query programming language.

A matrix can express conjunction, alternatives, exclusive alternatives and relative exclusion through **where terms are placed**.

This is a **stream/block filter**, not an assumption that the input is a relational database. Each input block (initially, one line) is tested as a whole. A block could include a full file path and its text content if the producer has represented those as one logical block.

The UI describes a left-to-right sequence of filtering stages, similar in purpose to a Unix pipeline. An implementation may evaluate the equivalent predicate in one pass.

## Basic rules

1. **Inside one cell:** all listed terms must occur in the same input block (AND).
2. **Within one column:** nonempty cells enumerate **allowed exact combinations** of the terms that appear anywhere in that column. One allowed combination is sufficient.
3. **Between columns:** an input block must pass **every** nonempty column. Visually, the output of one column flows into the next (AND / successive filtering).
4. **Row alignment across columns has no meaning.** A cell is not paired with the cell to its right merely because they occupy the same display row.
5. **Other input text is unconstrained.** Exclusivity applies only to the terms named in the same column.
6. An **empty cell is absent**, not an instruction to match every block or to match zero terms. A column with no nonempty cells can be skipped.

Square brackets in examples show cell boundaries; they are not user-entered operators. The UI can be an ordinary table.

## Example: a Unix-style filtering pipeline

~~~text
       Column 1           Column 2
     +-------------+     +---------+
     | autot       | --> | matti   |
     | mopot       |     |         |
     +-------------+     +---------+
          source  ----------> result
~~~

Column 1 recognizes the terms "autot" and "mopot" and allows two exact hit patterns:

- "autot" occurs and "mopot" does not.
- "mopot" occurs and "autot" does not.

A block containing **both** is rejected. This is the exclusive-or behaviour of the two named terms, without the user entering an XOR operator.

Column 2 recognizes only "matti". It requires "matti" but makes **no assertion** about "liisa", "seppo" or any other unlisted word.

For line-oriented matching:

| Input line | Result | Reason |
| --- | --- | --- |
| "matti autot" | pass | only "autot" in column 1; "matti" in column 2 |
| "matti mopot" | pass | only "mopot" in column 1; "matti" in column 2 |
| "matti autot mopot" | reject | both column-1 terms occur |
| "matti liisa autot" | pass | "liisa" is not constrained by either column |
| "liisa autot" | reject | no "matti" |

To accept **both** vehicle terms as well, add one cell in the **same column**:

~~~text
[autot      ] [matti]
[mopot      ] [     ]
[autot mopot] [     ]
~~~

Now "autot" only, "mopot" only and "autot mopot" together are all accepted, provided the block also contains "matti". This is inclusive OR, expressed by enumerating all three accepted combinations. No OR or XOR text is written in the matrix.

## Example: three names

~~~text
[seppo matti liisa] 
[matti            ]
[liisa            ]
~~~

The column vocabulary is {"seppo", "matti", "liisa"}. The permitted matched-term sets are:

- {"seppo", "matti", "liisa"}
- {"matti"}
- {"liisa"}

The following blocks therefore behave as shown:

| Names appearing in one block | Result |
| --- | --- |
| matti | pass |
| liisa | pass |
| seppo matti liisa | pass |
| matti liisa | reject |
| matti seppo | reject |
| seppo liisa | reject |
| seppo | reject |
| none of the three | reject |

The cell containing "matti" means **matti without the other named terms of that same column**. It does not mean "matti without any other person or word in the entire input".

Contrast this with a **single-cell column**:

~~~text
[matti]
~~~

Here the vocabulary contains only "matti". A block with "matti liisa" passes, because "liisa" is outside this column's vocabulary. In other words, this means **matti whether or not liisa occurs**.

## Multiple columns

~~~text
[seppo matti liisa] [autot      ] [2021]
[matti            ] [mopot      ] [2020]
[liisa            ] [autot mopot] [2019]
~~~

Column 1 selects one of its permitted name combinations. Column 2 further selects one of the three permitted vehicle combinations. Column 3 requires exactly one of the listed years, relative to the year terms in that column.

The rows do **not** form records. For example, the first name cell is not tied to the first vehicle cell or to 2021. Any accepted name combination can be paired with any accepted vehicle combination and any accepted year, as long as **all appear in the same input block**.

Thus the matrix is conceptually:

~~~text
stdin / block source
        |
  [names column]
        |
  [vehicles column]
        |
   [years column]
        |
      stdout
~~~

A text filter emits accepted input blocks unchanged and in their original order. The query does not group, sort, rewrite or infer field values from the input.

## Formal evaluation model

Let the input consist of blocks b in a universe U.

For each column j:

- V(j) is the set of **distinct** search terms appearing in all its nonempty cells.
- C(j) is the set of allowed cell term sets: one set of terms per nonempty cell.
- H(j, b) = { t in V(j) : t matches block b } is the **complete observed hit set** for that column.

The column accepts a block exactly when:

~~~text
H(j, b) is an element of C(j)
~~~

The full matrix accepts a block exactly when **every active column accepts it**:

~~~text
accept(b) = every active column j satisfies H(j, b) in C(j)
~~~

Equivalently, if F(j) is the set of blocks accepted by column j, the final result is the intersection of all F(j), applied to the original input universe.

This distinction matters: **ordinary OR of the cell predicates is not sufficient**. With cells ["autot"] and ["mopot"], a naive OR of substring searches would incorrectly accept a block containing both. The evaluation must compare the complete column-local hit set with the enumerated allowed combinations.

Although the visual evaluation is left to right, column intersection is mathematically commutative if all columns inspect the same unchanged block. An implementation may reorder or fuse checks for efficiency while preserving this exact behaviour.

## AND, OR, XOR and NOT without operator syntax

| Intended behaviour | Matrix construction |
| --- | --- |
| A and B together | one cell containing "A B" |
| A or B, but not both | separate cells "A" and "B" in one column |
| A or B, including both | cells "A", "B" and "A B" in one column |
| A with B absent | cells "A" and "A B" define the vocabulary, but to **allow only A**, use only the "A" combination in a column that also knows B (see scope note below) |
| Filter by condition X, then condition Y | X and Y in different columns |

**Scope note for absence:** a term absent from every cell is *not* part of the vocabulary. Consequently, an otherwise isolated cell ["A"] cannot mean "A but not B" if B is never named in that column. The examples above establish relative exclusion only when all relevant terms occur in the column's vocabulary. A future UI needs a non-operator way to register a term as **constrained but not an accepted cell combination** (for example, a column term palette or a disabled/forbidden combination). This is **not** yet specified. The proposed UI must not silently claim to implement unrestricted NOT or a pure "not B" predicate.

The ordinary mathematical XOR operation is a useful analogy for a column with two singleton alternatives. For three or more overlapping allowed patterns, the more exact term is **enumeration of allowed hit sets**, not repeatedly XOR-ing arbitrary cell predicates.

## Stream and matching boundaries

- **Input:** a sequence of independent logical blocks from stdin or another explicitly provided stream. The default proposal is one line per block, analogous to grep.
- **Output:** passing blocks, unchanged and in input order.
- **Match scope:** all cells inspect each whole block; a hit on one line does not combine with a hit on the next line.
- **Arbitrary content:** no database schema, file type, ownership relation or natural-language understanding is assumed. Terms are searches against bytes/text after a defined decoding and matching policy.
- **Term matching policy:** exact word, literal substring, case sensitivity, Unicode normalization and overlapping-term behaviour must be specified separately. The Boolean matrix semantics do not depend on which deterministic matching policy is chosen. Literal substring matching is a useful Unix-search default candidate, but could make both "auto" and "autot" match one occurrence.
- **File paths and contents:** they can participate in the same block if the producer combines them. Reading arbitrary filesystem objects requires separately delegated permissions; the filter never grants access.
- **Duplicate terms/cells:** repeated occurrences in one cell are logically redundant; duplicate accepted cells are redundant.
- **Empty patterns:** blank UI cells mean "no alternative here". To match the *absence of every named term* as an allowed pattern would require an explicit UI representation different from a blank cell.

These are **interface semantics**, not a claim that any particular BusyBox applet currently implements them.

## Relationship to Unix and Plan2001

The user-facing matrix is a compact alternative to assembling multi-stage grep, awk or shell expressions. A standalone userland command or graphical editor can compile the table to an internal predicate and operate over stdin/stdout. It should not require SQL, a prebuilt index or a database.

The design belongs entirely in **userland**. It does not require changes to the Plan2001 kernel, native ObjectSpace, capabilities, IPC or POSIX compatibility layer. If source blocks are produced from the filesystem, all traversal and file access still use the existing authorized userland mechanisms.

## Proposed acceptance tests (NOT RUN)

1. With vehicle cells ["autot"], ["mopot"], only one matching term passes; both terms together fail.
2. Adding ["autot mopot"] makes the both-term block pass.
3. With a second column ["matti"], every passing block must contain "matti"; the unlisted word "liisa" is allowed.
4. With name cells ["seppo matti liisa"], ["matti"], ["liisa"], blocks with exactly the enumerated name-term combinations pass, and other combinations fail.
5. A single-cell ["matti"] column accepts "matti liisa" because "liisa" is unlisted.
6. Moving a cell vertically within its own column does not change results.
7. Moving a cell to the same visual row as another column's cell does not correlate their matches.
8. Empty cells do not match or reject anything.
9. Each block is evaluated independently; matching words in adjacent lines cannot satisfy one cell.
10. Output preserves byte/text content and input order, subject to a separately chosen decoding/framing contract.

**Testing status:** NOT RUN. This is documentation of a proposed interface; no command, test fixture, QEMU run or hardware validation has been added.
