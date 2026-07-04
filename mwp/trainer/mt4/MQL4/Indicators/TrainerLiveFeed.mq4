#property strict
#property indicator_chart_window

input string TrainerUrl = "http://127.0.0.1";
input int PipScale = 100;
input int BootstrapBars = 10000;
input int BootstrapBatchSize = 1000;
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
		Print("TrainerLiveFeed request failed status=", status, " err=", GetLastError(), " url=", url);
		return false;
	}
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
		Print("TrainerLiveFeed empty request body url=", url);
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
		Print("TrainerLiveFeed body request failed status=", status, " err=", GetLastError(), " url=", url);
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

bool send_bootstrap_batch_range(int from_shift, int to_shift)
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
		"&recompute=0";
	return post_body(url, body, RecomputeTimeoutMs);
}

bool send_bootstrap_batches(int max_shift)
{
	int from_shift;

	if (max_shift <= 0) {
		return true;
	}
	if (BootstrapBatchSize <= 0) {
		Print("TrainerLiveFeed invalid BootstrapBatchSize=", BootstrapBatchSize);
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
		if (!send_bootstrap_batch_range(from_shift, to_shift)) {
			Print("TrainerLiveFeed bootstrap batch range failed from_shift=", from_shift, " to_shift=", to_shift);
			return false;
		}
		g_bootstrap_next_from_shift = to_shift - 1;
		Print("TrainerLiveFeed bootstrap batch sent from_shift=", from_shift, " to_shift=", to_shift, " remaining_from_shift=", g_bootstrap_next_from_shift);
	}
	return true;
}

void send_bootstrap()
{
	int max_shift;

	if (g_bootstrap_done) {
		return;
	}
	if (!BootstrapOnStartup) {
		g_bootstrap_done = true;
		return;
	}
	max_shift = MathMin(BootstrapBars, Bars - 1);
	if (!send_bootstrap_batches(max_shift)) {
		Print("TrainerLiveFeed bootstrap batch failed; will retry on next timer tick. requested=", max_shift);
		return;
	}
	if (max_shift > 0 && !recompute_live_stream()) {
		Print("TrainerLiveFeed bootstrap recompute failed; will retry bootstrap on next timer tick. requested=", max_shift);
		return;
	}
	if (Bars > 1) {
		g_last_sent_closed_time = Time[1];
	}
	g_bootstrap_done = true;
	g_bootstrap_next_from_shift = 0;
	Print("TrainerLiveFeed bootstrap complete symbol=", Symbol(), " timeframe=", timeframe_name(), " requested=", max_shift);
}

int OnInit()
{
	EventSetTimer(1);
	Print("TrainerLiveFeed initialized. Add ", TrainerUrl, " to MT4 Tools -> Options -> Expert Advisors -> Allow WebRequest.");
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
			Print("TrainerLiveFeed sent closed candle symbol=", Symbol(), " timeframe=", timeframe_name(), " ts=", (int)Time[1]);
		}
	}
}

int OnCalculate(const int rates_total,
	const int prev_calculated,
	const datetime &time[],
	const double &open[],
	const double &high[],
	const double &low[],
	const double &close[],
	const long &tick_volume[],
	const long &volume[],
	const int &spread[])
{
	return rates_total;
}
