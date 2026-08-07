---
name: what
description: Re-explain the agent's most recent message, question, or request in plain language with full standalone context, then re-ask any pending question. Use when the user invokes /what because the last message was unclear, jargon-heavy, or assumed knowledge of the code, a plan file, or earlier session scrollback.
argument-hint: [topic]
disable-model-invocation: true
---

# What

The user did not understand your last message. Rewrite it so it stands alone,
following the repository User Interaction rules:

- Use plain language; the user is not a domain expert. Where a technical term
  is unavoidable, explain it in one short sentence or parenthetical the first
  time it appears.
- Stick to the one established repository term for each concept and explain it
  once, instead of paraphrasing with synonyms — paraphrase variety is itself a
  source of confusion.
- Supply full context. The user has not read the source code, any plan file,
  or earlier session scrollback, and text emitted before a question-tool call
  may never be displayed — put everything needed to understand and answer into
  rendered message text the user is guaranteed to see, and only then ask.
- For any question or decision, state what the answer changes or blocks, and
  give options, trade-offs, and a recommendation where relevant.

## Workflow

1. Identify the target: the topic named in the argument, or with no argument
   your most recent message — especially any question or request still waiting
   on the user.
2. Restate it under the rules above, using headings and bullets so a longer
   explanation stays skimmable. Done when the restatement is answerable from
   this one message alone.
3. If the target contained a question or decision, re-ask it after that
   context is visible. Done when the pending question is either re-asked or
   confirmed not to exist.
