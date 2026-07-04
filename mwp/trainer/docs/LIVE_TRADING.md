# Live Trading Feed

This module connects a MetaTrader 4 chart to `trainer_daemon` and lets the GUI inspect live `layered_waves` classifications.

MT4 `WebRequest()` is not allowed from custom indicators. Use the Expert Advisor version for live feeding.

## Flow

1. MT4 indicator sends closed candles only.
2. Trainer persists candles under:

	`<storage_root>/live/<symbol>/<timeframe>/candles.csv`

3. Trainer stores stream metadata under:

	`<storage_root>/live/<symbol>/<timeframe>/meta.txt`

4. Metadata stores the original MT4 `symbol`, `timeframe`, and `pip_scale`. This keeps symbols with suffixes such as `XAUUSD.r` stable across daemon restarts even though directory names are sanitized.
5. Trainer recomputes `layered_waves` after each live closed candle.
6. During bootstrap/backfill, MT4 sends historical candles through `/live/candles_batch` with `recompute=0`, then calls `/live/recompute` once.
7. GUI `Live` tab polls `/live/status` and displays the selected stream waves/properties/chart.

## MT4 Setup

Copy:

	`trainer/mt4/MQL4/Experts/TrainerLiveFeedEA.mq4`

to the MT4 terminal experts directory:

	`MQL4/Experts/TrainerLiveFeedEA.mq4`

Then compile it in MetaEditor.

In MT4, enable WebRequest:

1. Open `Tools -> Options -> Expert Advisors`.
2. Enable `Allow WebRequest for listed URL`.
3. Add:

	`http://127.0.0.1`

Attach the Expert Advisor to the chart/timeframe you want to feed. The chart symbol and timeframe become the live stream identity in trainer.

## Indicator Inputs

- `TrainerUrl`: trainer daemon base URL, default `http://127.0.0.1`.
  A trailing slash is accepted by the indicator.
- `PipScale`: price-to-pips multiplier. For XAUUSD currently use `100`.
- `BootstrapBars`: number of historical closed bars sent on indicator startup. Default is `10000`.
- `BootstrapBatchSize`: number of candles per bootstrap HTTP batch. Default is `1000`.
- `BootstrapOnStartup`: when `true`, the indicator auto-populates the local live DB from MT4 history before normal live updates.
- `RequestTimeoutMs`: timeout for non-recomputing batch POSTs.
- `RecomputeTimeoutMs`: timeout for requests that trigger wave recomputation, including the one bootstrap recompute and ordinary live closed-candle updates.

## HTTP Endpoints

### Add Closed Candle

	POST /live/candle?symbol=XAUUSD&timeframe=M5&pip_scale=100&ts=1749553500&open=3329.64&high=3330.87&low=3326.93&close=3330.43&volume=997

Optional:

	`recompute=0`

When omitted, `recompute=1` is used.

### Add Candle Batch

	POST /live/candles_batch?symbol=XAUUSD&timeframe=M5&pip_scale=100&recompute=0

Body format is plain text CSV rows:

	1749553500,3329.64,3330.87,3326.93,3330.43,997
	1749553800,3330.43,3332.65,3328.83,3332.02,1234

Fields:

	`ts_epoch,open,high,low,close,volume`

The batch endpoint inserts or replaces all submitted candles, persists the stream once, and only recomputes if `recompute=1`.

### Recompute Stream

	POST /live/recompute?symbol=XAUUSD&timeframe=M5

### Stream Status

	GET /live/status

Important fields:

- `recompute_status`: `empty`, `stale`, or `fresh`.
- `waves_stale`: `1` when candles are newer than the current waves.
- `last_ts`: timestamp of the latest persisted candle.
- `last_recompute_ts`: latest candle timestamp included in the current waves.
- `last_recompute_ms`: elapsed time for the last wave recompute.
- `last_batch_input`: number of candles submitted in the latest live ingest call.
- `last_batch_changed`: number of candles inserted/replaced by the latest live ingest call.

### Stream Candles

	GET /live/candles?symbol=XAUUSD&timeframe=M5&start=0&count=512

### Stream Waves

	GET /live/waves?symbol=XAUUSD&timeframe=M5&start=0&count=128

### Stream Snapshot

	GET /live/snapshot?symbol=XAUUSD&timeframe=M5&candle_count=512&wave_count=128

Returns one coherent response containing:

- `summary`
- `candles`
- `waves`

The GUI `Live` tab uses this endpoint for selected-stream refreshes so the table and chart come from the same server-side state.

## GUI

Open the `Live` tab after `Predictor`.

- The stream selector shows `symbol`, `timeframe`, candle count, wave count, and recompute state.
- `Refresh Live` reloads status, waves, and chart.
- `Recompute Waves` manually recomputes the selected stream.
- `Reset Stream` deletes the selected stream from local live storage after confirmation. MT4 bootstrap can repopulate it.
- The right chart shows recent closed candles with wave overlays.
- The left wave table shows recent frontier waves.
- The bottom property grid shows all properties of the selected wave.
- If `waves_stale=1`, the chart still shows the latest closed candles, but wave overlays and wave selection are suppressed until recompute completes. This prevents stale wave boundaries from being presented as current frontier classification.

## Operational Notes

- The system assumes MT4 sends closed candles only.
- The indicator URL-encodes `symbol` and `timeframe`, so broker suffixes such as `XAUUSD.r` are preserved as stream identity.
- On startup, the indicator backfills up to `BootstrapBars` closed candles. If MT4 has fewer bars loaded in the chart, only the loaded history is sent.
- Bootstrap is deferred and batched: historical candles are sent to `/live/candles_batch` in chunks with `recompute=0`, then one `/live/recompute` runs after every chunk succeeds.
- If a bootstrap chunk fails, the indicator resumes from the failed chunk on the next timer tick instead of resending already accepted older chunks.
- If bootstrap recompute fails, the indicator retries bootstrap on the next timer tick.
- Same timestamp updates replace the existing candle.
- Newer timestamps append.
- Older unseen timestamps are inserted by timestamp order, allowing backfill.
- Read-only `/live/waves` and `/live/candles` do not create missing streams.
