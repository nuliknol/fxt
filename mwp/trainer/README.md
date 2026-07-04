# MT Trainer (Phase 5 Server Progress)

This folder contains the trainer server scaffold in C11 with async jobs for:

- threshold graph clustering
- k-medoids (custom mismatch distance)
- DBSCAN (custom mismatch distance)
- word mining (exact frequency mode, raw symbols or RLE symbols)
- phrase mining (exact repeated token-sequence mode, raw symbols or RLE symbols)

All training distance evaluation paths are GPU-only through HIP dispatch.

## Build

```bash
cd trainer
make -j
```

Build with HIP backend enabled:

```bash
cd trainer
make clean
make -j MT_ENABLE_HIP_BACKEND=1
```

If HIP backend is not enabled/available, clustering jobs fail by design.

## Run tests

```bash
cd trainer
make test
```

## Run daemon

```bash
cd trainer
sudo ./build/trainer_daemon 0.0.0.0 80 trainer_data
```

## API

- `GET /health`
- `GET /datasets`
- `GET /datasets/{dataset_id}`
- `GET /datasets/{dataset_id}/candles?start=0&count=512`
- `GET /datasets/{dataset_id}/features?start=0&count=512`
- `POST /datasets/import?path=/abs/path/file.csv&pip_scale=10000`
- `POST /jobs/character-clustering?dataset_id=1&q=4&delta=4&theta=4&mirrored=0&weighted=0&w_upper=1&w_lower=1&w_body=1&w_range=1`
- `POST /jobs/kmedoids?dataset_id=1&q=4&delta=4&k=8&max_iter=20&restarts=1&mirrored=0&weighted=0&w_upper=1&w_lower=1&w_body=1&w_range=1`
- `POST /jobs/dbscan?dataset_id=1&q=4&delta=4&eps=4&min_pts=4&mirrored=0&weighted=0&w_upper=1&w_lower=1&w_body=1&w_range=1`
- `POST /jobs/word-mining?dataset_id=1&min_len=2&max_len=12&use_rle=0`
- `POST /jobs/phrase-mining?dataset_id=1&min_len=2&max_len=8&use_rle=1`
- `GET /jobs`
- `GET /jobs/{job_id}`
- `POST /jobs/{job_id}/cancel`
- `GET /datasets/{dataset_id}/clusters`
- `GET /datasets/{dataset_id}/words`
- `GET /datasets/{dataset_id}/phrases`

## Notes

- Server code is pure C (snake_case, tabs).
- For 5-digit FX quotes, use `pip_scale=10000` so values are stored as integer pips (last fractional pip digit truncated).
- Heavy training stages are server-side only.
- Word/phrase mining currently depend on an existing clustering run (symbol stream derives from latest cluster ids).
- Job worker is asynchronous; query status by polling `/jobs` or `/jobs/{job_id}`.
