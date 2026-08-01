---
name: external-design-interface
description: >-
  Generate three radically different C++ interface designs, compare them, and
  synthesize the user's choice into a reviewed implementation plan. Use when
  the user explicitly asks to design an API, explore interface options, or
  design a system multiple ways, or when interface design for a new Collection,
  manager, or subsystem API is implicitly detected (ask before running); adding
  a member to an existing Collection routes to /add-collection-member instead.
allowed-tools: [Read, Grep, Glob, Agent, AskUserQuestion]
---

# Design Interface

Run from the main invoking context. A delegated worker never dispatches another
worker; if this skill is entered where delegation is forbidden, return a
main-context dispatch requirement instead of approximating the independent
designs inline.

## Establish the Design Brief

When interface design is only implicitly detected — a new Collection, manager,
or subsystem API — first ask "Would you like me to run
/external-design-interface to explore different API shapes?" and continue only
if accepted. Do not suggest or use this skill for bug fixes, single-function
additions, implementation of an already approved interface, or adding a member
to an existing Collection; that last case routes to `/add-collection-member`.

Use the supplied system description, or ask for one when absent. Inspect the
repository before asking about gaps. Record:

- problem, callers, operations, ownership, and existing interfaces;
- allocation tracking, SOA layout, threading, build affinity, and frame phases;
- bit-deterministic, CRC-checked PostRender state versus non-deterministic
  Interpolate/render state and client-only visuals.

Before any delegation, decide whether the requested system or a viable design
could add a `Collection<T>` type or SOA member pointer. If so, read
`/add-collection` and `/add-collection-member` and pass their applicable layout,
lifecycle, persistence, CRC, version, and registration constraints to every
worker.

## Research Existing Patterns

The main context dispatches one `researcher` first and waits for its result.
Give it the brief and exact repository scope. Ask it to identify similar live
systems, dominant caller patterns, naming and parameter conventions, manager
access, workbuffer usage, and relevant collection shapes. It returns concise
evidence with paths and symbols and does not delegate.

## Generate Three Designs

After research completes, the main context dispatches exactly three `planner`
workers concurrently. Use no more than the three available child slots and do
not add a manager/singleton-specific fourth design. Give each worker the same
brief, repository evidence, applicable collection constraints, and one
unsoftened axis:

1. Minimal surface: 1–3 entry points, opaque internals, deep module.
2. Data locality: contiguous SOA iteration, batching, and workbuffer use.
3. Caller ergonomics: optimize readability and simplicity for the dominant
   caller pattern found by research.

Each worker returns C++ interface declarations, representative usage, hidden
complexity, client/server guard impact, allocation behavior, threading and
frame-phase behavior, deterministic-state impact, and trade-offs. It explicitly
identifies any proposed Collection or member and never implements bodies.

## Compare and Decide

Present all three designs with their declarations, usage, and hidden
complexity. Compare interface simplicity and depth, SOA friendliness,
client/server gating, main-loop allocation, dispatch safety, frame-phase
clarity, and deterministic-state placement. Give an opinionated recommendation
grounded in the brief; do not rank by implementation effort.

Ask the user which design or explicit hybrid to carry forward. Do not silently
choose a public interface when multiple meaningfully different shapes remain.

## Synthesize and Gate the Plan

After the user decides, create a complete implementation plan containing the
final header-style interface, key call-site examples, incorporated elements,
affected integration sites and invariants, exclusions, and decisive acceptance
checks. Classify it under root `AGENTS.md` Change Workflow Steps 1 and 2, which
own the tier and the plan-review gates that tier requires.

Do not begin implementation from this skill. When the plan adds a Collection,
record that `/add-collection` owns its mechanical wiring and invokes
`/add-collection-member` for every SOA column.
