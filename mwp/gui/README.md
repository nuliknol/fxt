# MT GUI Client (Qt 5)

This directory contains the desktop client application (Qt/C++).

## Build

```bash
cd gui
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
cd gui
./build/mt_gui
```

## Features in this iteration

- Server health check and base URL configuration
- Dataset import/list/detail
- Candle and feature window requests
- Job submission for clustering and mining
- Job status table
- Cluster/word/phrase summary explorers

All heavy training/discovery remains server-side in `trainer`.
