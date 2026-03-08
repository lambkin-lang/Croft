# Spatial Workspace Questions

This note captures the questions Croft should answer before expanding the
spatial/zoomable workspace line further.

The key boundary is simple:

- document-centric editors are one product line
- spatial/zoomable workspaces are a different product line
- they may share document/editing capabilities, but they should not be planned
  as one blurred feature set

The current `example_zoom_canvas` proves that Croft has a viewport/camera path,
gesture plumbing, and scene-node hit-testing. It does not yet define a serious
Code Bubbles-style workspace product. This document exists to keep that gap
explicit.

## Product Definition Questions

1. Is the workspace meant to be a companion application beside the
   document-centric editor, or a larger shell that can host one or more editor
   instances inside it?
2. Is the primary goal exploration, authoring, review, teaching, debugging, or
   some blend of those modes?
3. Should the first serious workspace target a single developer on one machine,
   or should persistence/sharing/collaboration be part of the first design?
4. Is the workspace meant to replace a document tree and tabs, or to sit above
   them as a different navigation surface?

## Bubble and Node Questions

1. What is a node in the workspace: a whole file, a symbol, a text range, a
   live query result, a search result, or a mixed hierarchy?
2. Are nodes always editable, or are some of them previews/read-only views over
   shared underlying documents?
3. How do multiple nodes that point into the same document share selections,
   folding state, diagnostics, and edit history?
4. Should nodes be freeform spatial objects, snapped cards, or a hybrid?
5. What metadata belongs on a node besides text: title, file path, symbol path,
   diagnostics, search counts, connections, execution state?

## Zoom and Camera Questions

1. What should zoom mean semantically: pure camera scaling, semantic zoom,
   switching between document and summary views, or all of the above?
2. Should pinch zoom be continuous, stepped, or mode-dependent?
3. How should panning and zooming behave around the user's focal point: cursor,
   selection, viewport center, hovered node, or touch centroid?
4. At what zoom levels should text remain fully editable versus summarized or
   abstracted?
5. Does the workspace need an overview/minimap, bookmarks, or camera history
   before it becomes usable?

## Connection and Structure Questions

1. What kinds of relationships should be visualized first: call graph,
   references, imports, search results, task flow, notes, or user-defined links?
2. Are edges generated automatically, curated manually, or both?
3. How should connections react when the underlying code changes?
4. Should the first workspace understand only file/text structure, or should it
   wait for syntax-aware symbol structure from the editor pipeline?
5. What does collapsing or grouping nodes mean in a workspace that also has zoom?

## Editing and Interaction Questions

1. Does every node host the full editor command surface, or only a smaller
   subset tuned for workspace exploration?
2. How should undo behave across multiple nodes that may touch the same buffer?
3. What are the keyboard equivalents for pan, zoom, focus movement, node
   creation, and connection creation?
4. How should selection, focus, and caret ownership work when several editable
   nodes are visible at once?
5. Which interactions are essential for the first pass: drag, resize, collapse,
   duplicate, dock, link, filter?

## Persistence and Session Questions

1. What is the saved artifact: a workspace file, a generated view over project
   state, or both?
2. How much geometry should be durable versus recomputed?
3. How should workspaces survive file renames, symbol renames, and code motion?
4. Should the workspace save open nodes only, or also camera position, groups,
   filters, and temporary notes?
5. How should the workspace version its own saved structure as the product evolves?

## Accessibility and Input Questions

1. What is the accessibility story for a spatial code workspace on macOS before
   any broader platform ambitions?
2. How should screen readers represent nodes, edges, and zoom state?
3. Which gestures are required, and which should remain optional convenience
   paths?
4. How should the workspace degrade on systems without trackpads or gesture-rich
   input?
5. How much of the current gesture system should stay reusable substrate versus
   workspace-specific policy?

## Shared-Substrate Questions

1. Which capabilities should be shared with the document-centric editor line:
   document model, search, folding, syntax highlighting, diagnostics,
   file-open/save, clipboard?
2. Which capabilities should stay workspace-specific: camera, node layout,
   link routing, overview, semantic zoom?
3. Should workspace nodes embed the existing editor families directly, or should
   the workspace consume a smaller text-view capability instead?
4. How much of the workspace should be WIT-facing/mix-in based versus a more
   collapsed native shell while the concept is still fluid?
5. What benchmark signals matter for the workspace line: node count, link count,
   zoom latency, edit latency, saved-layout load time?

## Exit Criteria Before A Larger Implementation Pass

Croft should answer at least these points before building a larger workspace
demo:

1. what the first real user scenario is
2. what a node represents
3. what zoom means semantically
4. how editing and undo work across multiple nodes
5. what persistence artifact the workspace owns
6. which parts are shared with the document editor line versus kept separate
