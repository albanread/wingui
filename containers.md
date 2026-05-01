# Wingui Containers

This document summarizes the container and layout surfaces exposed by the declarative UI API in `include/wingui/ui_model.h`.

## Core Layout Containers

### `ui_stack(children)`

Vertical flow container.

- Lays children top-to-bottom.
- Supports container-level spacing through properties like `gap` and `padding`.
- Best default choice for forms, control panels, and one-column layouts.

Example:

```cpp
wg::ui_stack({
    wg::ui_heading("Controls"),
    wg::ui_slider("Hue", hue, "set-hue"),
    wg::ui_slider("Speed", speed, "set-speed")
})
```

### `ui_row(children)`

Horizontal flow container.

- Lays children left-to-right.
- Useful for short action bars, small field groups, and side-by-side controls.
- Current behavior is simple horizontal allocation, not a full flexbox-style layout engine.

Example:

```cpp
wg::ui_row({
    wg::ui_button("Play", "play"),
    wg::ui_button("Pause", "pause"),
    wg::ui_button("Stop", "stop")
})
```

### `ui_toolbar(children)`

Toolbar container.

- Publicly exposed as a distinct container type.
- Intended for command/grouped control surfaces.
- In the current native implementation it uses the same stack-style child layout path as `ui_stack`.

### `ui_scroll_view(children)`

Scrollable vertical viewport.

- Hosts child content in a vertically scrollable container.
- Useful for long settings panels, inspectors, and sidebars that may exceed the window height.
- Current implementation is a stack-style content flow inside a local vertical scroll region.

Example:

```cpp
wg::ui_scroll_view({
    wg::ui_form({
        wg::ui_input("Title", title, "set-title"),
        wg::ui_textarea("Notes", notes, "set-notes")
    })
})
```

### `ui_grid(columns, children)`

Fixed-column grid container.

- Lays children out row-major across the requested number of columns.
- Useful for dashboards, property sheets, thumbnail groups, and compact control layouts.
- Supports `padding`, `gap`, and the `columns` count.

Example:

```cpp
wg::ui_grid(2, {
    wg::ui_button("One", "one"),
    wg::ui_button("Two", "two"),
    wg::ui_button("Three", "three"),
    wg::ui_button("Four", "four")
})
```

### `ui_form(children)`

Form-oriented vertical container.

- Intended for labeled inputs and editor panels.
- Behaves like a stack with a form-friendly default spacing profile.
- Good default wrapper for groups of `ui_input`, `ui_slider`, `ui_select`, and similar controls.

Example:

```cpp
wg::ui_form({
    wg::ui_input("Name", name, "set-name"),
    wg::ui_number_input("Count", count, "set-count"),
    wg::ui_checkbox("Enabled", enabled, "set-enabled")
})
```

### `ui_card(title, children)`

Titled grouped container.

- Wraps a set of children with a title and built-in padding.
- Good for grouping related settings or information into sections.
- Internally behaves like a padded vertical container under the title.

Example:

```cpp
wg::ui_card("Playback", {
    wg::ui_slider("Speed", speed, "set-speed"),
    wg::ui_checkbox("Loop", loop, "toggle-loop")
})
```

### `ui_divider()`

Visual separator.

- Not a full container.
- Useful inside `ui_stack`, `ui_row`, or `ui_card` to break sections apart.

## Split Layout

### `ui_split_view(orientation, event_name, first_pane, second_pane, ...)`

Resizable two-pane split container.

- Supports `"horizontal"` and `"vertical"` orientation.
- Emits splitter events through `event_name`.
- Supports explicit width, height, divider size, live resize, and disabled state in the extended overload.
- This is the main layout for editor/sidebar, canvas/control-panel, and master/detail surfaces.

Example:

```cpp
wg::ui_split_view(
    "horizontal",
    "main-split",
    wg::ui_split_pane("canvas", 0.75, 320, 0, false, true, {
        wg::ui_rgba_pane("canvas", 640, 480)
    }),
    wg::ui_split_pane("controls", 0.25, 220, 0, false, false, {
        wg::ui_stack({
            wg::ui_slider("Hue", hue, "set-hue"),
            wg::ui_slider("Speed", speed, "set-speed")
        })
    })
)
```

### `ui_split_pane(id, ...)`

Pane wrapper used inside `ui_split_view`.

- Holds the content for one side of a split.
- Supports `size`, `min_size`, `max_size`, `collapsed`, and `focused`.
- Use `size` to express pane proportions such as `0.75` and `0.25`.

## Higher-Level Container Widgets

These are not generic layout primitives, but they do contain child content or structure other content.

### `ui_tabs(label, value, event_name, tabs, ...)`

Tabbed container.

- Organizes content into named tabs.
- Better when the user should switch between mutually exclusive content views.

### `ui_table(label, columns, rows, event_name)`

Structured tabular container.

- Best for records, lists, and inspection views.
- Uses helper builders such as `ui_column(...)` and `ui_table_row(...)`.

### `ui_tree_view(...)`

Hierarchical collection container.

- Intended for explorer-style navigation and nested data.
- Supports selection and expanded item state.

### `ui_context_menu(items, content)`

Context-menu wrapper around content.

- Associates a menu model with a content node.
- More of a behavior wrapper than a page layout primitive.

## What To Use When

- Use `ui_stack` for most settings panels and vertical forms.
- Use `ui_form` when the content is primarily labeled inputs.
- Use `ui_row` for short horizontal groups.
- Use `ui_grid` when content should scan in two or more columns.
- Use `ui_scroll_view` when a panel may outgrow its viewport.
- Use `ui_card` when a group needs a visible titled boundary.
- Use `ui_split_view` when two regions need persistent space and user resizing.
- Use `ui_tabs` when only one of several panels should be visible at a time.
- Use `ui_table` or `ui_tree_view` for structured data, not for general page layout.

## Current Practical Layout Model

If you think in terms of page composition, the current practical layout toolbox is:

- `ui_stack` for vertical composition
- `ui_form` for input-heavy vertical composition
- `ui_row` for horizontal composition
- `ui_grid` for fixed-column composition
- `ui_scroll_view` for vertical overflow handling
- `ui_split_view` plus `ui_split_pane` for resizable composition
- `ui_card` and `ui_toolbar` as styled/grouped container variants

That is the effective layout system today.