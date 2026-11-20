# Assemble the port spec

Type: task
Status: resolved
Blocked by: 04
Map: [LBP3 native port](../map.md)

## Question

Write the **port spec**, the destination of this map, at `.scratch/lbp3-port/spec.md`, from the closed tickets' answers. It covers:

- scope, including what is out of scope
- the base, and how the fork tracks upstream
- the toolchain and how to run the merged dump
- the gap table
- the milestone ladder, with the first milestone fully defined
- the known risks

This collects decisions already made; it decides nothing new. If writing it turns up a gap, open a ticket for the gap instead of filling it in here.

Done when the spec exists, links each section to the ticket that decided it, and can be handed to the to-tickets skill to cut build tickets for the first milestone.

## Answer

Done on 2026-11-10: the [port spec](../spec.md) is final. It was assembled from the resolved tickets (01–04, 06–10), decides nothing new, and links each section to the ticket that decided it. Milestone 1 is fully defined (steps and five checks), so build tickets can be cut from it.
