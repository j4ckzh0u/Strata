# llama.cpp on an Intel Mac with a discrete AMD card

The Mac engine (`metal/`) needs no llama.cpp. This folder is for people who run llama.cpp itself on the same hardware
(for example to compare, or to run a model Strata's engine does not support).

Measured on a MacBook Pro16,1 (2019), Intel Core i9-9980HK,64 GB RAM, AMD Radeon Pro5500M8 GB, macOS15.7.5.

## What breaks without it

llama.cpp's Metal backend assumes Apple Silicon. A discrete AMD card in an Intel Mac has no unified memory, no
`MTLGPUFamilyApple7` (so no `simdgroup_matrix` fast path) and no Metal4. Three things then go wrong:

| Symptom | Cause | Fix here |
| --- | --- | --- |
| `GGML_ASSERT(buf_src) failed` in `ggml_metal_buffer_set_tensor` | a private Metal buffer copies from a host pointer that is not page-aligned, and `newBufferWithBytesNoCopy` returns nil | stage unaligned data in a page-aligned copy |
| Wrong tokens (repeated BOS, garbage) although the CPU decodes correctly | `MTLDispatchTypeConcurrent` command buffers race on this GPU: the memory barriers do not order the dispatches | `GGML_METAL_CONCURRENCY_DISABLE=1`, a switch that already exists |
|1.55 tokens/s and the GPU barely used | `ggml_metal_buffer_map` wraps the mmapped GGUF as `MTLResourceStorageModeShared`: the weights stay in system RAM, and every decode step reads them over PCIe (measured1.4 to1.85 GB/s) | `GGML_METAL_WEIGHTS_VRAM=1` copies them into a private (VRAM) buffer at load |

The patch adds the page-aligned staging and `GGML_METAL_WEIGHTS_VRAM`, and makes a mapped buffer take the interface that
matches its storage (the shared interface asserts on a private buffer).

## Apply

```sh
cd /path/to/llama.cpp # at commit3cf03257, the one this repository pins
git apply /path/to/0001-metal-intel-mac-amd.patch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON
ninja -C build llama-server
```

Run:

```sh
GGML_METAL_WEIGHTS_VRAM=1 GGML_METAL_SHARED_BUFFERS_DISABLE=1 GGML_METAL_CONCURRENCY_DISABLE=1 \
 ./build/bin/llama-server -m model.gguf -ngl99 -c8192 --host127.0.0.1 --port8080
```

`GGML_METAL_SHARED_BUFFERS_DISABLE=1` is optional: without it the KV and compute buffers stay in system RAM. With it they
stay in VRAM too, which measured9.6 against7.4 tokens/s at a1.4K-token context.

The first start compiles the Metal libraries on the CPU (about65 seconds here); later starts reuse the driver's cache.

## Measured

Qwen3.5-4B Q4_K_M (2.74 GB), greedy,8K context, `-ngl99`:

| Configuration | Decode | Prompt |
| --- | ---: | ---: |
| llama.cpp as shipped (weights mapped shared) |1.55 tok/s |2.1 tok/s |
| CPU only (`-ngl0`, the i9) |5.93 tok/s |19.0 tok/s |
| weights in VRAM, KV in RAM |15.4 tok/s |34.6 tok/s |
| weights and KV/compute in VRAM |23.1 tok/s |35.9 tok/s |

A model larger than the card's `maxBufferLength` (3.76 GB here) is copied in chunks, and it must still fit the8.57 GB
working set for this speed: a4B or7B Q4 does, a16 GB model does not.

## Not a speed-up here

`--spec-type draft-mtp` needs a model that carries MTP/NextN tensors, and Qwen3.5-4B has none. The ngram
self-speculative types measured22.4 (`ngram-simple`) to24.0 (`ngram-mod`) tokens/s against22.6 without them, on a
repeating-line prompt at temperature0; `ngram-cache` was slower (14.4).
