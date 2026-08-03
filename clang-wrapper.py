#!/usr/bin/env python3
import sys
import subprocess

# Filter out /permissive- flag and skip the first argument (compiler path from launcher)
args = [arg for arg in sys.argv[2:] if arg != "/permissive-"]

# Call the real clang++ compiler
result = subprocess.call([r"C:\Program Files\LLVM\bin\clang++.exe"] + args)
sys.exit(result)
