#include "core_api.h"
#include "live_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_log(const char *msg)
{
	printf("[live_store_tests] %s\n", msg);
}

static Candle make_candle(int32_t ts, int32_t open_pips, int32_t close_pips)
{
	Candle c;
	int32_t high = open_pips > close_pips ? open_pips : close_pips;
	int32_t low = open_pips < close_pips ? open_pips : close_pips;

	c.ts_epoch = ts;
	c.open_pips = open_pips;
	c.high_pips = high + 10;
	c.low_pips = low - 10;
	c.close_pips = close_pips;
	c.volume = 100;
	return c;
}

static void test_upsert_persists_and_reloads(void)
{
	char storage_root[256];
	char err[256];
	LiveStreamSummary summary;
	LiveStreamSummary items[8];
	uint32_t count = 0u;
	uint32_t dataset_id = 0u;
	Candle candles[8];
	uint32_t written = 0u;
	int rc;

	test_log("test_upsert_persists_and_reloads");
	snprintf(storage_root, sizeof(storage_root), "/tmp/trainer-live-store-test-%ld", (long)getpid());
	rc = core_init(storage_root);
	assert(rc == 0);

	{
		Candle c0 = make_candle(1749553500, 332964, 333043);
		Candle c1 = make_candle(1749553800, 333043, 333202);
		Candle c2 = make_candle(1749554100, 333202, 333281);

		rc = live_store_upsert_candle("XAUUSD", "M5", 100u, &c0, &summary, err, sizeof(err));
		printf("[live_store_tests] upsert c0 rc=%d err=%s candles=%u waves=%u\n", rc, err, summary.candle_count, summary.wave_count);
		assert(rc == 0);
		rc = live_store_upsert_candle("XAUUSD", "M5", 100u, &c1, &summary, err, sizeof(err));
		printf("[live_store_tests] upsert c1 rc=%d err=%s candles=%u waves=%u\n", rc, err, summary.candle_count, summary.wave_count);
		assert(rc == 0);
		rc = live_store_upsert_candle("XAUUSD", "M5", 100u, &c2, &summary, err, sizeof(err));
		printf("[live_store_tests] upsert c2 rc=%d err=%s candles=%u waves=%u\n", rc, err, summary.candle_count, summary.wave_count);
		assert(rc == 0);
		assert(summary.pip_scale == 100u);
		assert(summary.candle_count == 3u);
		assert(summary.last_ts == c2.ts_epoch);
		assert(summary.last_batch_input == 1u);
		assert(summary.last_batch_changed == 1u);
		assert(summary.last_recompute_ts == c2.ts_epoch);
		assert(summary.waves_stale == 0);
		assert(summary.recompute_status == LIVE_RECOMPUTE_FRESH);
	}

	rc = live_store_get_dataset_id("UNKNOWN", "M5", &dataset_id);
	printf("[live_store_tests] unknown stream lookup rc=%d\n", rc);
	assert(rc != 0);

	rc = live_store_get_status(items, 8u, &count);
	assert(rc == 0);
	assert(count == 1u);
	assert(items[0].pip_scale == 100u);
	assert(items[0].candle_count == 3u);
	assert(items[0].waves_stale == 0);
	assert(items[0].recompute_status == LIVE_RECOMPUTE_FRESH);
	rc = live_store_get_dataset_id("XAUUSD", "M5", &dataset_id);
	assert(rc == 0);
	rc = core_get_candles(dataset_id, 0u, 8u, candles, &written);
	assert(rc == 0);
	assert(written == 3u);
	assert(candles[0].open_pips == 332964);
	assert(candles[2].close_pips == 333281);
	core_shutdown();

	rc = core_init(storage_root);
	assert(rc == 0);
	rc = live_store_get_status(items, 8u, &count);
	printf("[live_store_tests] reload status rc=%d count=%u pip_scale=%u candles=%u\n",
		rc,
		count,
		count > 0u ? items[0].pip_scale : 0u,
		count > 0u ? items[0].candle_count : 0u);
	assert(rc == 0);
	assert(count == 1u);
	assert(items[0].pip_scale == 100u);
	assert(items[0].candle_count == 3u);
	rc = live_store_get_dataset_id("XAUUSD", "M5", &dataset_id);
	assert(rc == 0);
	rc = core_get_candles(dataset_id, 0u, 8u, candles, &written);
	assert(rc == 0);
	assert(written == 3u);
	assert(candles[0].open_pips == 332964);
	assert(candles[2].close_pips == 333281);
	core_shutdown();
}

static void test_deferred_recompute(void)
{
	char storage_root[256];
	char err[256];
	LiveStreamSummary summary;
	uint32_t dataset_id = 0u;
	int rc;

	test_log("test_deferred_recompute");
	snprintf(storage_root, sizeof(storage_root), "/tmp/trainer-live-store-deferred-test-%ld", (long)getpid());
	rc = core_init(storage_root);
	assert(rc == 0);
	{
		Candle c0 = make_candle(1749560000, 100000, 100100);
		Candle c1 = make_candle(1749560300, 100100, 100220);
		Candle c2 = make_candle(1749560600, 100220, 100320);

		rc = live_store_upsert_candle_ex("XAUUSD", "M5", 100u, &c0, 0, &summary, err, sizeof(err));
		printf("[live_store_tests] deferred c0 rc=%d err=%s candles=%u waves=%u\n", rc, err, summary.candle_count, summary.wave_count);
		assert(rc == 0);
		assert(summary.candle_count == 1u);
		assert(summary.wave_count == 0u);
		assert(summary.waves_stale == 1);
		assert(summary.recompute_status == LIVE_RECOMPUTE_STALE);
		rc = live_store_upsert_candle_ex("XAUUSD", "M5", 100u, &c1, 0, &summary, err, sizeof(err));
		assert(rc == 0);
		assert(summary.candle_count == 2u);
		assert(summary.wave_count == 0u);
		rc = live_store_upsert_candle_ex("XAUUSD", "M5", 100u, &c2, 0, &summary, err, sizeof(err));
		assert(rc == 0);
		assert(summary.candle_count == 3u);
		assert(summary.wave_count == 0u);
		assert(summary.waves_stale == 1);
		assert(summary.recompute_status == LIVE_RECOMPUTE_STALE);
		rc = live_store_get_dataset_id("XAUUSD", "M5", &dataset_id);
		assert(rc == 0);
		rc = live_store_recompute("XAUUSD", "M5", &summary, err, sizeof(err));
		printf("[live_store_tests] deferred recompute rc=%d err=%s candles=%u waves=%u dataset=%u\n",
			rc,
			err,
			summary.candle_count,
			summary.wave_count,
			dataset_id);
		assert(rc == 0);
		assert(summary.candle_count == 3u);
		assert(summary.wave_count > 0u);
		assert(summary.waves_stale == 0);
		assert(summary.recompute_status == LIVE_RECOMPUTE_FRESH);
	}
	core_shutdown();
}

static void test_batch_ingest_deferred_recompute(void)
{
	char storage_root[256];
	char err[256];
	LiveStreamSummary summary;
	Candle candles[4];
	uint32_t changed_count = 0u;
	int rc;

	test_log("test_batch_ingest_deferred_recompute");
	snprintf(storage_root, sizeof(storage_root), "/tmp/trainer-live-store-batch-test-%ld", (long)getpid());
	rc = core_init(storage_root);
	assert(rc == 0);
	candles[0] = make_candle(1749570000, 200000, 200100);
	candles[1] = make_candle(1749570300, 200100, 200250);
	candles[2] = make_candle(1749570600, 200250, 200200);
	candles[3] = make_candle(1749570900, 200200, 200350);
	rc = live_store_upsert_candles_ex("XAUUSD",
		"M5",
		100u,
		candles,
		4u,
		0,
		&changed_count,
		&summary,
		err,
		sizeof(err));
	printf("[live_store_tests] batch deferred rc=%d err=%s changed=%u candles=%u waves=%u stale=%d\n",
		rc,
		err,
		changed_count,
		summary.candle_count,
		summary.wave_count,
		summary.waves_stale);
	assert(rc == 0);
	assert(changed_count == 4u);
	assert(summary.candle_count == 4u);
	assert(summary.last_batch_input == 4u);
	assert(summary.last_batch_changed == 4u);
	assert(summary.wave_count == 0u);
	assert(summary.waves_stale == 1);
	assert(summary.recompute_status == LIVE_RECOMPUTE_STALE);
	rc = live_store_recompute("XAUUSD", "M5", &summary, err, sizeof(err));
	assert(rc == 0);
	assert(summary.wave_count > 0u);
	assert(summary.waves_stale == 0);
	assert(summary.recompute_status == LIVE_RECOMPUTE_FRESH);
	rc = live_store_upsert_candles_ex("XAUUSD",
		"M5",
		100u,
		candles,
		4u,
		0,
		&changed_count,
		&summary,
		err,
		sizeof(err));
	printf("[live_store_tests] batch duplicate rc=%d err=%s changed=%u candles=%u waves=%u stale=%d\n",
		rc,
		err,
		changed_count,
		summary.candle_count,
		summary.wave_count,
		summary.waves_stale);
	assert(rc == 0);
	assert(changed_count == 0u);
	assert(summary.candle_count == 4u);
	assert(summary.last_batch_input == 4u);
	assert(summary.last_batch_changed == 0u);
	assert(summary.waves_stale == 0);
	core_shutdown();
}

static void test_reset_removes_persisted_stream(void)
{
	char storage_root[256];
	char err[256];
	LiveStreamSummary summary;
	LiveStreamSummary items[8];
	Candle candles[2];
	uint32_t count = 0u;
	uint32_t changed_count = 0u;
	int rc;

	test_log("test_reset_removes_persisted_stream");
	snprintf(storage_root, sizeof(storage_root), "/tmp/trainer-live-store-reset-test-%ld", (long)getpid());
	rc = core_init(storage_root);
	assert(rc == 0);
	candles[0] = make_candle(1749580000, 300000, 300120);
	candles[1] = make_candle(1749580300, 300120, 300260);
	rc = live_store_upsert_candles_ex("XAUUSD",
		"M5",
		100u,
		candles,
		2u,
		1,
		&changed_count,
		&summary,
		err,
		sizeof(err));
	printf("[live_store_tests] reset setup rc=%d err=%s changed=%u candles=%u waves=%u\n",
		rc,
		err,
		changed_count,
		summary.candle_count,
		summary.wave_count);
	assert(rc == 0);
	assert(summary.candle_count == 2u);
	rc = live_store_get_status(items, 8u, &count);
	assert(rc == 0);
	assert(count == 1u);
	core_shutdown();

	rc = core_init(storage_root);
	assert(rc == 0);
	rc = live_store_reset("XAUUSD", "M5");
	printf("[live_store_tests] reset rc=%d\n", rc);
	assert(rc == 0);
	rc = live_store_get_status(items, 8u, &count);
	printf("[live_store_tests] reset status rc=%d count=%u\n", rc, count);
	assert(rc == 0);
	assert(count == 0u);
	core_shutdown();
}

static void test_reload_preserves_original_symbol_identity(void)
{
	char storage_root[256];
	char err[256];
	LiveStreamSummary summary;
	LiveStreamSummary items[8];
	Candle candle;
	uint32_t count = 0u;
	uint32_t dataset_id = 0u;
	int rc;

	test_log("test_reload_preserves_original_symbol_identity");
	snprintf(storage_root, sizeof(storage_root), "/tmp/trainer-live-store-symbol-test-%ld", (long)getpid());
	rc = core_init(storage_root);
	assert(rc == 0);
	candle = make_candle(1749590000, 400000, 400150);
	rc = live_store_upsert_candle("XAUUSD.r", "M5", 100u, &candle, &summary, err, sizeof(err));
	printf("[live_store_tests] symbol identity setup rc=%d err=%s symbol=%s timeframe=%s candles=%u\n",
		rc,
		err,
		summary.symbol,
		summary.timeframe,
		summary.candle_count);
	assert(rc == 0);
	assert(strcmp(summary.symbol, "XAUUSD.r") == 0);
	assert(strcmp(summary.timeframe, "M5") == 0);
	core_shutdown();

	rc = core_init(storage_root);
	assert(rc == 0);
	rc = live_store_get_status(items, 8u, &count);
	printf("[live_store_tests] symbol identity reload rc=%d count=%u symbol=%s timeframe=%s\n",
		rc,
		count,
		count > 0u ? items[0].symbol : "",
		count > 0u ? items[0].timeframe : "");
	assert(rc == 0);
	assert(count == 1u);
	assert(strcmp(items[0].symbol, "XAUUSD.r") == 0);
	assert(strcmp(items[0].timeframe, "M5") == 0);
	rc = live_store_get_dataset_id("XAUUSD.r", "M5", &dataset_id);
	assert(rc == 0);
	rc = live_store_get_dataset_id("XAUUSD_r", "M5", &dataset_id);
	assert(rc != 0);
	core_shutdown();
}

int main(void)
{
	test_log("starting live store tests");
	test_upsert_persists_and_reloads();
	test_deferred_recompute();
	test_batch_ingest_deferred_recompute();
	test_reset_removes_persisted_stream();
	test_reload_preserves_original_symbol_identity();
	test_log("all live store tests passed");
	return 0;
}
