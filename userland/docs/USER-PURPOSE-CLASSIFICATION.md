# Classification of Computer Use by User Purpose

> **Status: current working description and application-review specification, not a target state or an implementation claim.**
>
> **Scope:** Plan2001 userland, application selection, product scope, and future user-experience decisions.
>
> **Date:** 2026-11-17

## 1. Purpose and standing

This document records the project's **current understanding of what people use computers for**. It is a description of existing use, not a statement that Plan2001 ought to implement every use case.

For the present, Plan2001 adopts four user-purpose categories:

- **A — Experiencing:** consuming media, entertainment, leisure, or other experiences.
- **B — Knowing:** seeking information, learning, investigating, or understanding.
- **C — Doing:** creating, changing, operating, maintaining, or accomplishing something.
- **D — Connecting:** communicating and interacting with other people.

These four categories are the **current classification premise**, rather than a proposed product goal. The model is open to revision if an actual use case cannot be described adequately. Neither logical reasoning nor the examples in this document constitute a formal empirical proof that the classification is exhaustive.

**An important decision is still open:** whether Plan2001 should serve all four purposes within one environment, support only a subset, or offer different environments/profiles for different purposes. This document does **not** make that decision.

The actionable requirement is narrower: **each new application considered for Plan2001 must be evaluated against the four categories**, and its intended user purpose must be recorded. Applications need not support all categories.

## 2. Classification unit: purpose, not program

Classify the **user's intended outcome** in a use case, not an executable name, window, system call, file type, or hardware form factor.

The same browser may be used to watch a movie (A), read technical documentation (B), edit a document online (C), or join a video call (D). Conversely, a single purpose may involve several applications and services.

The classification applies to phones, desktops, laptops, servers, graphical applications, command-line programs, and background processes. It is independent of whether an application has a visible window.

Use the category definitions below at the level of a **user-intended activity**. Do not classify an operation by all of its incidental technical effects:

- Reading a page normally changes memory and caches; that alone does not make the activity C.
- Sending an email involves composing and storing bytes; if the purpose is person-to-person communication, it belongs to D.
- A server transmits data, but its network traffic does not by itself make it D: a background HTTP service performing a delegated operational task normally belongs to C.
- Looking up information to complete a project can belong to both B and C if both purposes are relevant to the described activity.

### 2.1 Category definitions

| ID | Name | User's purpose | Typical examples |
| --- | --- | --- | --- |
| A | Experiencing | To experience content, an event, or leisure activity | Film, music, games, entertainment browsing |
| B | Knowing | To obtain or improve information or understanding | Search, encyclopedia, research, documentation, asking an AI for an explanation |
| C | Doing | To produce, alter, execute, control, maintain, or accomplish an intended result | Write a quotation, edit code, manage a project, control equipment, operate an HTTP service |
| D | Connecting | To establish or maintain interpersonal communication or interaction | Email, messaging, voice/video calls, collaborative discussion |

**Doing is not synonymous with paid work.** Hobby programming, personal administration, artistic creation, and unattended services can be C. Work, study, and leisure are **contexts**, not replacements for A–D.

**Connecting concerns interaction with people.** Machine-to-machine protocols are not automatically D; determine the service's user-level purpose instead.

The categories describe intentions and may overlap within a single activity. The examples are illustrative, not a complete list of application types.

## 3. Logical model: four independent classification dimensions

Represent the active purposes of an activity or a collection of simultaneous activities with four Boolean propositions:

- A: Experiencing is an intended purpose.
- B: Knowing is an intended purpose.
- C: Doing is an intended purpose.
- D: Connecting is an intended purpose.

A proposition may be true alongside any of the others. **No exclusivity constraint is imposed.** An application can be assigned several categories; multiple activities can also be aggregated into one session classification.

All 16 Boolean combinations are syntactically available in the model:

| A | B | C | D | Active purposes |
| :-: | :-: | :-: | :-: | --- |
| 0 | 0 | 0 | 0 | None recorded |
| 1 | 0 | 0 | 0 | Experiencing |
| 0 | 1 | 0 | 0 | Knowing |
| 0 | 0 | 1 | 0 | Doing |
| 0 | 0 | 0 | 1 | Connecting |
| 1 | 1 | 0 | 0 | Experiencing, knowing |
| 1 | 0 | 1 | 0 | Experiencing, doing |
| 1 | 0 | 0 | 1 | Experiencing, connecting |
| 0 | 1 | 1 | 0 | Knowing, doing |
| 0 | 1 | 0 | 1 | Knowing, connecting |
| 0 | 0 | 1 | 1 | Doing, connecting |
| 1 | 1 | 1 | 0 | Experiencing, knowing, doing |
| 1 | 1 | 0 | 1 | Experiencing, knowing, connecting |
| 1 | 0 | 1 | 1 | Experiencing, doing, connecting |
| 0 | 1 | 1 | 1 | Knowing, doing, connecting |
| 1 | 1 | 1 | 1 | Experiencing, knowing, doing, connecting |

The all-zero value means that **none of the four purposes is currently recorded** for the chosen scope. It may correspond to an idle state, an unclassified activity, or an incomplete observation; it must not be taken as proof that no real user purpose exists.

A concrete all-true session could contain a film playing in one window, Wikipedia open in another, a quotation being written in a third, and an email being sent. A background HTTP server can also be active under C without an application window.

**Logical independence is not statistical independence.** Real purposes may frequently co-occur, and certain ways of describing an activity can make categories overlap. The four bits are a permissive description, not a claim about measured usage frequencies or a theorem about human behavior.

## 4. Scope decision for Plan2001

The classification describes present computer use. It **does not prescribe** Plan2001's product boundary.

Future development must explicitly consider the architectural and product question:

> Should Plan2001 serve A, B, C, and D within the same operating environment?

Possible decisions include:

1. **Unified environment:** support all four purposes as concurrent activities.
2. **Deliberate subset:** support selected purposes without pretending to cover the others.
3. **Multiple environments or profiles:** provide distinct userland configurations for different purposes.
4. **Incremental coverage:** implement one subset now while leaving the final scope undecided.

None is mandated by this document. Do not infer a requirement for a desktop, browser, multimedia stack, messaging system, or other specific feature merely from the existence of a category.

When deciding coverage, examine actual user scenarios, resource cost, performance, security boundaries, hardware needs, complexity, maintenance burden, and interaction between simultaneous activities. Record an explicit scope decision before treating coverage as a product requirement.

## 5. Mandatory review of each proposed new application

**Every new application proposed for Plan2001 MUST include a four-purpose assessment in its proposal, issue, or pull request description.** This is a documentation/review requirement, not a requirement to add runtime classification machinery.

For each application:

1. **State the intended user scenario and outcome.** Describe why the user runs it rather than merely naming the executable.
2. **Mark A, B, C, and D as applicable or not applicable.** Multiple positive classifications are valid; no single 'primary category' is required.
3. **Give concrete reasons and examples** for each applicable category. Distinguish the application's user-level purposes from incidental implementation details.
4. **Describe interaction mode:** windowed, command-line, service/daemon, unattended, remote, or a combination. A window is not a prerequisite for a user purpose.
5. **Identify dependencies and effects:** needed objects/data, external services, devices, permissions, persistent state, background execution, and communication. Classification does not grant these permissions.
6. **State the product-scope implication:** whether the application fits a supported category, expands coverage, or raises the unresolved question of serving all four in one environment.
7. **Flag an inadequate classification.** If A–D cannot accurately describe the use case, report the counterexample instead of forcing a category or inventing a hidden fifth category.

This assessment should be short for trivial utilities and more detailed for applications that combine multiple purposes or introduce major dependencies.

### 5.1 Review template

Copy the following fields into a new-application proposal:

| Field | Required answer |
| --- | --- |
| Application / component | Name |
| User scenario and intended result | Short description |
| A — Experiencing | Yes/No, with reason |
| B — Knowing | Yes/No, with reason |
| C — Doing | Yes/No, with reason |
| D — Connecting | Yes/No, with reason |
| Mode of operation | GUI / CLI / background / remote / other |
| Dependencies and authority | Data, objects, devices, network, capabilities |
| Plan2001 scope decision | In scope / out of scope / undecided, with reason |
| Classification gap | None / describe an actual counterexample |

### 5.2 Examples

| Application or service | Typical user scenario | Classification |
| --- | --- | --- |
| Video player | Watch a film | A |
| Encyclopedia reader | Understand a technical topic | B |
| Text editor | Prepare a quotation | C |
| Email client | Communicate with another person | D |
| HTTP server | Operate a published service unattended | C |
| Web browser | Watch, research, edit, or communicate | A / B / C / D, depending on the use case |
| Collaborative editor | Produce a document and interact with collaborators | C + D |
| Diagnostic utility | Investigate a problem and possibly fix it | B, C, or B + C depending on the task |

These assignments are **scenario-specific**, not immutable properties of program binaries.

## 6. Requirements boundaries and non-goals

- This document **MUST** be considered when reviewing new applications.
- This document **MUST NOT** be interpreted as a decision to implement all four categories in a single Plan2001 environment.
- This document **MUST NOT** be interpreted as a requirement to build four desktop modes, four window classes, a category-aware scheduler, or a new kernel interface.
- A–D are **not security labels**, process types, capability classes, application sandbox profiles, or authorization rules.
- Classification **MUST NOT** confer access to objects, memory, devices, network services, or other resources.
- Classification **MUST NOT** override Plan2001's native ObjectSpace and capability authorization or move Unix/POSIX projections into the kernel.
- Existing applications are not claimed to have been reviewed retroactively; coverage can be assessed incrementally.
- Applications outside the current scope are not intrinsically invalid. Product scope and user-purpose classification are separate decisions.

The current userland remains the experimental Unix-facing projection described in [ARCHITECTURE.md](ARCHITECTURE.md). This document adds **no code, ABI, runtime data format, user interface, or kernel requirement**.

## 7. Validation and change control

The four-way division is accepted here as the project's **present working description** of computer use. It is not an immutable axiom and is not presented as a formally demonstrated exhaustive taxonomy.

Review the model when:

- an actual application has a clear purpose not covered by A–D;
- two category definitions consistently yield unavoidable contradictions;
- user research reveals important missing distinctions;
- a proposed product scope or interface needs a finer-grained classification.

Changes should record the counterexample, proposed adjustment, and consequences for earlier classifications. Avoid changing the categories merely to match an existing implementation.

**Acceptance for this document:** future application proposals include an explicit A–D assessment and separate their classification from decisions about product scope and system permissions.

**Implementation/testing status:** documentation only. No source changes, runtime enforcement, automated tests, QEMU tests, or hardware tests are introduced or claimed.

---
*This specification is a decision aid for future Plan2001 development. The four user purposes describe computer use as currently understood; whether Plan2001 supports all of them together remains open.*
