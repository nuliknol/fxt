#property strict

input string TrainerUrl = "http://192.168.1.14";
input int PipScale = 100;
input int BootstrapBars = 10000;
input int BootstrapBatchSize = 1000;
input int StartupOverlapBars = 256;
input int RequestTimeoutMs = 5000;
input int RecomputeTimeoutMs = 120000;
input bool BootstrapOnStartup = true;

datetime g_last_sent_closed_time = 0;
bool g_bootstrap_done = false;
int g_bootstrap_target_shift = 0;
int g_bootstrap_next_from_shift = 0;

string timeframe_name()
{
	int period = Period();

	if (period == PERIOD_M1) {
		return "M1";
	}
	if (period == PERIOD_M5) {
		return "M5";
	}
	if (period == PERIOD_M15) {
		return "M15";
	}
	if (period == PERIOD_M30) {
		return "M30";
	}
	if (period == PERIOD_H1) {
		return "H1";
	}
	if (period == PERIOD_H4) {
		return "H4";
	}
	if (period == PERIOD_D1) {
		return "D1";
	}
	if (period == PERIOD_W1) {
		return "W1";
	}
	if (period == PERIOD_MN1) {
		return "MN1";
	}
	return IntegerToString(period);
}

string trainer_base_url()
{
	string url = TrainerUrl;

	while (StringLen(url) > 0 && StringSubstr(url, StringLen(url) - 1, 1) == "/") {
		url = StringSubstr(url, 0, StringLen(url) - 1);
	}
	return url;
}

string url_encode(string value)
{
	string out = "";
	int i;

	for (i = 0; i < StringLen(value); ++i) {
		int ch = StringGetChar(value, i);

		if ((ch >= 'A' && ch <= 'Z') ||
			(ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			ch == '_' || ch == '-' || ch == '.' || ch == '~') {
			out += CharToString((uchar)ch);
		} else {
			out += StringFormat("%%%02X", ch & 255);
		}
	}
	return out;
}

bool post_empty(string url, int timeout_ms)
{
	string headers = "";
	char data[];
	char result[];
	string result_headers;
	int status;

	status = WebRequest("POST",
		url,
		headers,
		timeout_ms,
		data,
		result,
		result_headers);
	if (status != 200) {
		Print("TrainerLiveFeedEA request failed status=", status, " err=", GetLastError(), " url=", url);
		return false;
	}
	return true;
}

bool http_get_text(string url, int timeout_ms, int &status_out, string &body_out)
{
	string headers = "";
	char data[];
	char result[];
	string result_headers;
	int status;

	ArrayResize(data, 0);
	body_out = "";
	status_out = -1;
	ResetLastError();
	status = WebRequest("GET",
		url,
		headers,
		timeout_ms,
		data,
		result,
		result_headers);
	status_out = status;
	if (status < 0) {
		Print("TrainerLiveFeedEA GET request failed status=", status, " err=", GetLastError(), " url=", url);
		return false;
	}
	body_out = CharArrayToString(result, 0, ArraySize(result));
	return true;
}

bool post_body(string url, string body, int timeout_ms)
{
	string headers = "Content-Type: text/plain\r\n";
	char data[];
	char result[];
	string result_headers;
	int status;
	int len;

	len = StringToCharArray(body, data, 0, StringLen(body));
	if (len <= 0) {
		Print("TrainerLiveFeedEA empty request body url=", url);
		return false;
	}
	status = WebRequest("POST",
		url,
		headers,
		timeout_ms,
		data,
		result,
		result_headers);
	if (status != 200) {
		Print("TrainerLiveFeedEA body request failed status=", status, " err=", GetLastError(), " url=", url);
		return false;
	}
	return true;
}

bool send_closed_bar(int shift, bool recompute)
{
	string url;
	int timeout_ms;

	if (shift <= 0 || shift >= Bars) {
		return false;
	}
	url = trainer_base_url() +
		"/live/candle?symbol=" + url_encode(Symbol()) +
		"&timeframe=" + url_encode(timeframe_name()) +
		"&pip_scale=" + IntegerToString(PipScale) +
		"&ts=" + IntegerToString((int)Time[shift]) +
		"&open=" + DoubleToString(Open[shift], Digits) +
		"&high=" + DoubleToString(High[shift], Digits) +
		"&low=" + DoubleToString(Low[shift], Digits) +
		"&close=" + DoubleToString(Close[shift], Digits) +
		"&volume=" + IntegerToString((int)Volume[shift]) +
		"&recompute=" + (recompute ? "1" : "0");
	timeout_ms = recompute ? RecomputeTimeoutMs : RequestTimeoutMs;
	return post_empty(url, timeout_ms);
}

bool recompute_live_stream()
{
	string url = trainer_base_url() +
		"/live/recompute?symbol=" + url_encode(Symbol()) +
		"&timeframe=" + url_encode(timeframe_name());

	return post_empty(url, RecomputeTimeoutMs);
}

string candle_csv_row(int shift)
{
	return IntegerToString((int)Time[shift]) + "," +
		DoubleToString(Open[shift], Digits) + "," +
		DoubleToString(High[shift], Digits) + "," +
		DoubleToString(Low[shift], Digits) + "," +
		DoubleToString(Close[shift], Digits) + "," +
		IntegerToString((int)Volume[shift]) + "\n";
}

int find_closed_bar_shift(datetime ts)
{
	int shift;

	if (ts <= 0) {
		return -1;
	}
	for (shift = 1; shift < Bars; ++shift) {
		if (Time[shift] == ts) {
			return shift;
		}
	}
	return -1;
}

int json_find_int_field(string body, string key, int fallback)
{
	string pattern = "\"" + key + "\":";
	int pos = StringFind(body, pattern);
	string tail;

	if (pos < 0) {
		return fallback;
	}
	pos += StringLen(pattern);
	tail = StringSubstr(body, pos);
	return StrToInteger(tail);
}

bool fetch_remote_last_ts(int &last_ts_out)
{
	string url = trainer_base_url() +
		"/live/summary?symbol=" + url_encode(Symbol()) +
		"&timeframe=" + url_encode(timeframe_name());
	string body;
	int status;

	last_ts_out = 0;
	if (!http_get_text(url, RequestTimeoutMs, status, body)) {
		return false;
	}
	if (status == 404) {
		return true;
	}
	if (status != 200) {
		Print("TrainerLiveFeedEA live summary failed status=", status, " body=", body);
		return false;
	}
	last_ts_out = json_find_int_field(body, "last_ts", 0);
	return true;
}

bool send_bootstrap_batch_range(int from_shift, int to_shift, string reason)
{
	string url;
	string body = "";
	int shift;

	if (from_shift < to_shift || to_shift <= 0) {
		return true;
	}
	for (shift = from_shift; shift >= to_shift; --shift) {
		body += candle_csv_row(shift);
	}
	url = trainer_base_url() +
		"/live/candles_batch?symbol=" + url_encode(Symbol()) +
		"&timeframe=" + url_encode(timeframe_name()) +
		"&pip_scale=" + IntegerToString(PipScale) +
		"&recompute=0" +
		"&reason=" + url_encode(reason);
	return post_body(url, body, RecomputeTimeoutMs);
}

bool send_bootstrap_batches(int max_shift)
{
	int from_shift;

	if (max_shift <= 0) {
		return true;
	}
	if (BootstrapBatchSize <= 0) {
		Print("TrainerLiveFeedEA invalid BootstrapBatchSize=", BootstrapBatchSize);
		return false;
	}
	if (g_bootstrap_target_shift != max_shift || g_bootstrap_next_from_shift <= 0) {
		g_bootstrap_target_shift = max_shift;
		g_bootstrap_next_from_shift = max_shift;
	}
	for (from_shift = g_bootstrap_next_from_shift; from_shift >= 1; from_shift -= BootstrapBatchSize) {
		int to_shift = from_shift - BootstrapBatchSize + 1;

		if (to_shift < 1) {
			to_shift = 1;
		}
		if (!send_bootstrap_batch_range(from_shift, to_shift, "bootstrap")) {
			Print("TrainerLiveFeedEA bootstrap batch range failed from_shift=", from_shift, " to_shift=", to_shift);
			return false;
		}
		g_bootstrap_next_from_shift = to_shift - 1;
		Print("TrainerLiveFeedEA bootstrap batch sent from_shift=", from_shift, " to_shift=", to_shift, " remaining_from_shift=", g_bootstrap_next_from_shift);
	}
	return true;
}

bool send_batch_window(int from_shift, int to_shift, string reason)
{
	int batch_from;

	if (from_shift < to_shift || to_shift <= 0) {
		return true;
	}
	if (BootstrapBatchSize <= 0) {
		Print("TrainerLiveFeedEA invalid BootstrapBatchSize=", BootstrapBatchSize);
		return false;
	}
	for (batch_from = from_shift; batch_from >= to_shift; batch_from -= BootstrapBatchSize) {
		int batch_to = batch_from - BootstrapBatchSize + 1;

		if (batch_to < to_shift) {
			batch_to = to_shift;
		}
		if (!send_bootstrap_batch_range(batch_from, batch_to, reason)) {
			Print("TrainerLiveFeedEA batch range failed from_shift=", batch_from, " to_shift=", batch_to);
			return false;
		}
	}
	return true;
}

bool restore_recent_overlap(string reason)
{
	int overlap_from_shift;

	if (Bars <= 1) {
		return false;
	}
	overlap_from_shift = MathMin(Bars - 1, StartupOverlapBars);
	if (overlap_from_shift < 1) {
		return true;
	}
	Print("TrainerLiveFeedEA restoring recent overlap symbol=",
		Symbol(),
		" timeframe=",
		timeframe_name(),
		" overlap_from_shift=",
		overlap_from_shift,
		" reason=",
		reason);
	if (!send_batch_window(overlap_from_shift, 1, reason)) {
		Print("TrainerLiveFeedEA overlap restore failed from_shift=", overlap_from_shift, " to_shift=1 reason=", reason);
		return false;
	}
	if (!recompute_live_stream()) {
		Print("TrainerLiveFeedEA overlap restore recompute failed from_shift=", overlap_from_shift, " reason=", reason);
		return false;
	}
	g_last_sent_closed_time = Time[1];
	Print("TrainerLiveFeedEA overlap restore complete symbol=",
		Symbol(),
		" timeframe=",
		timeframe_name(),
		" restored_count=",
		overlap_from_shift,
		" reason=",
		reason);
	return true;
}

bool sync_remote_history()
{
	int remote_last_ts = 0;
	int remote_shift;
	int missing_from_shift;
	int max_shift;

	if (Bars <= 1) {
		return false;
	}
	if (!fetch_remote_last_ts(remote_last_ts)) {
		return false;
	}
	if (remote_last_ts <= 0) {
		max_shift = MathMin(BootstrapBars, Bars - 1);
		if (!send_bootstrap_batches(max_shift)) {
			Print("TrainerLiveFeedEA bootstrap batch failed; will retry on next timer tick. requested=", max_shift);
			return false;
		}
		if (max_shift > 0 && !recompute_live_stream()) {
			Print("TrainerLiveFeedEA bootstrap recompute failed; will retry bootstrap on next timer tick. requested=", max_shift);
			return false;
		}
		g_last_sent_closed_time = Time[1];
		g_bootstrap_next_from_shift = 0;
		Print("TrainerLiveFeedEA bootstrap complete symbol=", Symbol(), " timeframe=", timeframe_name(), " requested=", max_shift);
		return true;
	}
	if (remote_last_ts >= (int)Time[1]) {
		return restore_recent_overlap("up_to_date_overlap");
	}
	remote_shift = find_closed_bar_shift((datetime)remote_last_ts);
	if (remote_shift < 0) {
		max_shift = MathMin(BootstrapBars, Bars - 1);
		g_bootstrap_target_shift = 0;
		g_bootstrap_next_from_shift = 0;
		Print("TrainerLiveFeedEA remote last_ts not found locally; re-running bootstrap symbol=", Symbol(), " timeframe=", timeframe_name(), " last_ts=", remote_last_ts);
		if (!send_bootstrap_batches(max_shift)) {
			Print("TrainerLiveFeedEA fallback bootstrap failed; will retry on next timer tick. requested=", max_shift);
			return false;
		}
		if (max_shift > 0 && !recompute_live_stream()) {
			Print("TrainerLiveFeedEA fallback bootstrap recompute failed; will retry on next timer tick. requested=", max_shift);
			return false;
		}
		g_last_sent_closed_time = Time[1];
		g_bootstrap_next_from_shift = 0;
		return true;
	}
	missing_from_shift = remote_shift - 1;
	if (missing_from_shift < 1) {
		return restore_recent_overlap("tail_aligned_overlap");
	}
	Print("TrainerLiveFeedEA restoring missing closed candles symbol=",
		Symbol(),
		" timeframe=",
		timeframe_name(),
		" missing_from_shift=",
		missing_from_shift,
		" remote_last_ts=",
		remote_last_ts,
		" local_last_closed_ts=",
		(int)Time[1]);
	if (!send_batch_window(missing_from_shift, 1, "gap_restore")) {
		Print("TrainerLiveFeedEA gap restore failed from_shift=", missing_from_shift, " to_shift=1");
		return false;
	}
	if (!recompute_live_stream()) {
		Print("TrainerLiveFeedEA gap restore recompute failed missing_from_shift=", missing_from_shift);
		return false;
	}
	if (!restore_recent_overlap("post_gap_overlap")) {
		return false;
	}
	Print("TrainerLiveFeedEA restored missing closed candles symbol=", Symbol(), " timeframe=", timeframe_name(), " restored_count=", missing_from_shift, " remote_last_ts=", remote_last_ts);
	return true;
}

void send_bootstrap()
{
	if (g_bootstrap_done) {
		return;
	}
	if (!BootstrapOnStartup) {
		g_bootstrap_done = true;
		return;
	}
	if (!sync_remote_history()) {
		return;
	}
	g_bootstrap_done = true;
}

int OnInit()
{
	EventSetTimer(1);
	Print("TrainerLiveFeedEA initialized. Add ", TrainerUrl, " to MT4 Tools -> Options -> Expert Advisors -> Allow WebRequest.");
	return INIT_SUCCEEDED;
}

void OnDeinit(const int reason)
{
	EventKillTimer();
}

void OnTimer()
{
	send_bootstrap();
	if (Bars > 1 && Time[1] != g_last_sent_closed_time) {
		if (send_closed_bar(1, true)) {
			g_last_sent_closed_time = Time[1];
			Print("TrainerLiveFeedEA sent closed candle symbol=", Symbol(), " timeframe=", timeframe_name(), " ts=", (int)Time[1]);
		}
	}
}
