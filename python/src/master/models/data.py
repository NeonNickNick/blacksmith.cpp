"""Array contracts for native engine output and training batches."""
from typing import TypeAlias

import numpy as np
from numpy.typing import NDArray

Floats: TypeAlias = NDArray[np.float32]
Ints: TypeAlias = NDArray[np.int32]
History: TypeAlias = list[tuple[Floats, Floats, Floats]]
Prefix: TypeAlias = list[list[int]]
