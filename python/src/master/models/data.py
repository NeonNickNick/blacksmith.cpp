"""Array contracts for native engine output and training batches."""
# 这里只约定数组和轨迹的类型，不复制 C++ 编码逻辑或创建训练数据。
from typing import TypeAlias

import numpy as np
from numpy.typing import NDArray

Floats: TypeAlias = NDArray[np.float32]
Ints: TypeAlias = NDArray[np.int32]
# History 的每一项是一回合开始时双方的 (状态、合法掩码、搜索策略)。
# 单项形状分别为 (2, observation长度)、(2, 动作数)、(2, 动作数)。
History: TypeAlias = list[tuple[Floats, Floats, Floats]]
# Prefix 每项为双方完整动作编号 [first_action, second_action]，不含监督标签。
Prefix: TypeAlias = list[list[int]]
