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
