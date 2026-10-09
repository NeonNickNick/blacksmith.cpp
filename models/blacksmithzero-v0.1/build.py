"""Build the repository rule adapter and C++ inference example."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent

def main():
    command = ['cmake', '-S', str(ROOT), '-B', str(ROOT/'build'), '-DCMAKE_BUILD_TYPE=Release']
    env = os.environ.copy()
    if os.name == 'nt' and not (ROOT/'build/CMakeCache.txt').exists():
        compiler = shutil.which('g++')
        if not compiler:
            candidate = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))/'w64devkit/bin/g++.exe'
            if candidate.exists():
                compiler = str(candidate)
        if compiler:
            directory = str(Path(compiler).parent)
            env['PATH'] = directory + os.pathsep + env['PATH']
            command += ['-G', 'MinGW Makefiles', '-DCMAKE_CXX_COMPILER='+compiler,
                        '-DCMAKE_MAKE_PROGRAM='+str(Path(directory)/'mingw32-make.exe')]
    subprocess.run(command, check=True, env=env)
    subprocess.run(['cmake','--build',str(ROOT/'build'),'--config','Release','--parallel','4'], check=True, env=env)

if __name__ == '__main__':
    main()
