# UI Record / Playback Macros

## Purpose

This document explores a future direction for `wingui` and the hosted declarative UI model: recording user interaction as structured UI events and replaying those interactions later as macros, test flows, or application control scripts.

This is a concept note only. It does not propose immediate implementation.

## Why This Fits The Current Model

The current UI stack already has the right shape for record and playback:

- the UI is described as a retained declarative tree
- controls have stable ids or stable structural paths
- native controls emit structured event payloads rather than raw screen coordinates
- the model produces structured patch or publish updates in response
- the native host can now log both incoming events and outgoing patch decisions

That is a much better foundation than traditional GUI automation.

Instead of saying "click pixel `(420, 177)`", this model can say:

- change `name` to `"Grace Hopper"`
- toggle `enabled` to `false`
- set `meeting_date` to `"2026-05-17"`
- activate tab `choices`

Those actions are semantic and tied to the UI model rather than to fragile screen geometry.

## Core Idea

The key idea is to treat UI interaction as an append-only event stream with enough structure to support three separate uses:

1. diagnostics
2. repeatable testing
3. user-facing macros

The same event journal could later support all three, with different playback policies.

## Recording Model

At a high level, a recording session would capture:

- timestamp or relative time offset
- event source, such as `native-win32` or model-injected playback
- event name
- control id
- payload value
- optional surrounding context

Example event shapes:

```json
{
  "t": 0,
  "event": "name",
  "id": "name",
  "value": "Grace Hopper"
}
```

```json
{
  "t": 420,
  "event": "enabled",
  "id": "enabled",
  "value": false
}
```

```json
{
  "t": 760,
  "event": "main_tab",
  "id": "main_tab",
  "value": "choices"
}
```

The important point is that the recording should capture intent rather than physical gesture details where possible.

## Playback Modes

There are at least three useful playback modes.

### 1. Event replay

Replay the recorded UI events back through the normal application event path.

This is the best mode for validating that the application reacts to input consistently.

Advantages:

- closest to a real user path
- exercises model update logic
- preserves application-level event handling

Weaknesses:

- timing-sensitive flows may need guardrails
- can still depend on whether the target control exists and is in the expected state

### 2. State mutation replay

Translate recorded events into direct model mutations and rerender.

This is closer to what the recent patch probe does.

Advantages:

- deterministic
- ideal for diff/patch diagnostics
- avoids native gesture complexity

Weaknesses:

- bypasses some control-specific behavior
- not a full substitute for event-driven validation

### 3. High-level scripted actions

Normalize a recorded session into more explicit actions such as:

- `set_text(id, value)`
- `select_tab(id, value)`
- `toggle(id, checked)`
- `select_row(id, row_id)`

This is the most useful form for end-user macros.

Advantages:

- readable
- editable
- stable across layout or skin changes

Weaknesses:

- requires a normalization layer
- may not preserve exact low-level interaction details

## User Macro Recorder

One practical future feature is a built-in user macro recorder.

The user experience could look like this:

1. start recording
2. interact with the application normally
3. stop recording
4. save the macro with a name
5. replay it later from a command palette, button, or menu

Examples of user-facing use cases:

- repetitive data entry flows
- switching to a known workspace layout
- batch form population
- recurring administrative tasks
- demo scripts for support or training

The important distinction is that this would not be screen automation in the usual sense. It would be application-native intent capture.

## Why This Could Matter For Application Control

If the event stream is sufficiently stable, record/playback becomes a control surface.

That opens up future possibilities such as:

- headless or semi-headless scripted runs
- remote control over IPC
- assistive tooling
- task automation driven by another application
- agent-driven workflows that operate through the same semantic event layer

In that model, the UI is no longer only a presentation surface. It becomes a controllable interface contract.

## What Makes This More Reliable Than Traditional UI Automation

Traditional GUI automation often depends on:

- screen coordinates
- timing assumptions
- focus state
- fragile caption matching
- OS widget quirks

This model can avoid much of that if it relies on ids and event payloads.

The more the system can anchor playback to explicit control ids and model semantics, the more robust it becomes against:

- window resizing
- theme changes
- DPI changes
- font changes
- partial layout refactors

## Preconditions For A Good Design

This only works well if a few rules stay true.

### Stable ids matter

Controls that should participate in recording or playback need stable ids.

If ids are missing and the system falls back to structural auto-paths only, playback becomes more fragile under layout changes.

### Events need clear semantics

A recorder should prefer canonical event payloads.

For example:

- text input should record the final intended value
- checkbox and switch controls should record the final boolean state
- list and tab selection should record selected ids or values
- tree controls should record selected ids and expanded ids, not only raw gestures

### Side effects should remain application-owned

Playback should drive the same public event and model path that real input uses. It should not directly mutate arbitrary internal state unless the explicit playback mode is state-based diagnostics.

## Determinism And Synchronization

Playback cannot rely on raw timing alone.

A robust design would eventually need synchronization points such as:

- wait until control `x` exists
- wait until tab `y` is selected
- wait until patch reconciliation completes
- wait until a specific model state value appears

That suggests a future macro format should support both actions and expectations.

Example:

```json
{
  "action": "set_text",
  "id": "name",
  "value": "Grace Hopper",
  "expect": {
    "model": { "name": "Grace Hopper" }
  }
}
```

## Diagnostics Value Even Without User Macros

Even if user-facing macros are never shipped, the same machinery is already valuable for engineering.

It can support:

- regression reproduction
- bug capture from real user sessions
- deterministic replay of troublesome interaction sequences
- comparing model patches against native patch behavior
- validating whether a UI bug is caused by event generation, model reconciliation, or native patch application

This is exactly the kind of leverage that the recent patch probe and native tracing started to demonstrate.

## Risks And Tradeoffs

The concept is promising, but it does come with real design constraints.

### Over-recording implementation details

If recordings capture too much low-level noise, they become brittle and hard to reason about.

### Under-recording context

If recordings capture too little context, playback may fail in ways that are hard to diagnose.

### Version drift

If the UI tree evolves substantially, older macros may need migration logic or compatibility checks.

### Security and privacy

A real recorder would need explicit treatment of sensitive values such as passwords, secrets, tokens, and personal data.

## A Sensible Future Direction

If this is pursued later, the likely order would be:

1. continue improving native event and patch traces for diagnostics
2. define a canonical event journal format
3. add internal engineering replay for deterministic bug reproduction
4. add assertions and synchronization points
5. only then consider end-user macro authoring and playback UX

That sequence keeps the early investment useful even if user-facing macros take longer or never ship.

## Summary

The declarative `wingui` model appears unusually well-suited to UI record and playback.

The reason is simple: the system already moves through structured ids, structured events, structured model state, and structured patch documents. That creates a realistic path to:

- stable UI diagnostics
- replayable regression scenarios
- semantic automation
- future user macro recorders
- eventual application control through the UI contract itself

It is worth keeping this direction in mind as the native UI model matures, even if implementation is deferred.