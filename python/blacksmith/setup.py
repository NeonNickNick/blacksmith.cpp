"""Tag wheels containing CMake-built modules for their actual platform."""
# C++ 扩展由外部 CMake 流程构建；这里仅修正打包元信息，不再触发编译。
from setuptools import Distribution, setup


class BinaryDistribution(Distribution):
    def has_ext_modules(self) -> bool:
        # 即使 setup() 没有声明 ext_modules，也不能将已有原生库打成纯 Python wheel。
        return True


setup(distclass=BinaryDistribution)
