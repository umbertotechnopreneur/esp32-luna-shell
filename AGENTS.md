# Repository guidance

## Code comments

- Put a short, practical comment immediately above each non-obvious function or method declaration/definition.
- Use that signature comment to clarify purpose, inputs/outputs, side effects, or an important constraint.
- Add comments inside a function only for genuinely complex control-flow steps.
- Explain intent or trade-offs; do not restate what the code already says.
- Leave obvious accessors, boilerplate, simple wrappers, and self-explanatory operations uncommented.
- Keep comments close to the relevant code and concise. Do not write long explanatory blocks.

## Scope and safety

- Preserve unrelated work already present in the working tree.
- Treat `managed_components/`, vendor/Waveshare components, generated assets, and build output as external or generated code unless a task explicitly targets them.
- Do not build, flash, or monitor a device unless the user explicitly requests it.
- When a device port is already known, use it directly; do not run repeated serial, PnP, or USB enumeration probes before flashing or monitoring.
- Treat ESP USB/JTAG enumeration as potentially disruptive on Windows. After a disconnect, stop probing and wait for the user to reconnect the device.
- Run at most one requested flash or monitor attempt per confirmed connection. If it fails or the port disappears, report it instead of retrying in a loop.

## Git workflow

- While Umberto is the sole developer, do not open pull requests for this repository.
- After the requested checks pass, commit directly to the intended branch and push only when explicitly authorized.
- Before a high-risk migration, preserve the verified baseline with an explicit tag or backup branch.

## Embedded stack discipline

- Keep task stack usage small; do not place large snapshots, arrays, image data, or formatting buffers in local variables.
- Reuse an existing snapshot by const reference instead of copying it through nested calls.
- Put large temporary buffers in bounded PSRAM allocations and handle allocation failure without falling back to internal RAM.
- Defer heavy refresh or persistence work until after LVGL event callbacks unwind.
- Prefer reducing stack frames over increasing task stack sizes; account for scarce internal RAM before changing a task stack.
- Add or inspect a stack high-water mark when a task gains a non-trivial call path, and validate it on device.
