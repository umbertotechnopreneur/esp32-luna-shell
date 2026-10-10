# Character model

Rig geometry, expression catalog and motion/touch response. Consumes canonical
snapshot inputs; never reads physical sensors. Placement is composed separately
from deformation so scenes can reuse the same response model.

Public contracts are in `include/`; authored rig data stays under `assets/cody`.
See [rendering](../../../../docs/cody/rendering.md).
