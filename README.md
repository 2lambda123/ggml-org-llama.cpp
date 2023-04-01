# rwkv.cpp

This is a port of [BlinkDL/RWKV-LM](https://github.com/BlinkDL/RWKV-LM) to [ggerganov/ggml](https://github.com/ggerganov/ggml). The end goal is to allow 4-bit quanized inference on CPU.

**WORK IN PROGRESS!** **Status**: FP32 and FP16 inference work correctly. Currently, I'm working on creating usable C library interface and Python wrapper for it.

## Plan

1. Create proper interface (probably, C library)
2. Create Python wrapper with sampling and simple chat interface
3. Write a good `README.md` and publish links to this repo
4. Make INT4 inference work
5. Create pull request to main `ggml` repo with all improvements made here

## Structure

This repo is based on the [llama.cpp repo](https://github.com/ggerganov/llama.cpp). RWKV-related code is in these directories:

- `./rwkv`: directory containing Python scripts for conversion and validation
- `./examples/main_rwkw`: directory containing script that loads and infers RWKV model

Please do not change files in other directories — this will make pulling recent changes easier.

## How to use

### Windows

Requirements: [git](https://gitforwindows.org/), [CMake](https://cmake.org/download/), MSVC compiler, Python 3.x with PyTorch.

Clone the repo and set it up for build:

```commandline
git clone https://github.com/saharNooby/rwkv.cpp.git
cd rwkv.cpp
cmake .
```

Download an RWKV model from [Huggingface](https://huggingface.co/BlinkDL) and convert it into `ggml` format:

```commandline
python rwkv\convert_pytorch_rwkv_to_ggml.py C:\RWKV-4-Pile-169M-20220807-8023.pth C:\rwkv.cpp-169M.bin float32
```

Compile and run the script:

```commandline
cmake --build . --config Release
bin\Release\main_rwkv.exe "C:\rwkv.cpp-169M.bin" 123 "C:\state_in.bin" "C:\state_out.bin" "C:\logits_out.bin"
```

The script will read state from `state_in.bin`, do single inference using the state and token `123` as an input, save new state into `state_out.bin` and logits into `logits_out.bin`.
