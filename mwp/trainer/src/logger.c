#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_log_fp;
static char g_log_path[256];

static void log_vwrite(const char *level, const char *fmt, va_list ap)
{
	time_t now;
	struct tm *tm_now;
	char ts[32];

	pthread_mutex_lock(&g_log_mu);
	if (g_log_fp == NULL) {
		pthread_mutex_unlock(&g_log_mu);
		return;
	}

	now = time(NULL);
	tm_now = localtime(&now);
	if (tm_now != NULL) {
		strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_now);
	} else {
		snprintf(ts, sizeof(ts), "1970-01-01 00:00:00");
	}
	fprintf(g_log_fp, "[%s] [%s] ", ts, level);
	vfprintf(g_log_fp, fmt, ap);
	fputc('\n', g_log_fp);
	fflush(g_log_fp);
	pthread_mutex_unlock(&g_log_mu);
}

int trainer_log_init(void)
{
	uint32_t rnd;

	pthread_mutex_lock(&g_log_mu);
	if (g_log_fp != NULL) {
		pthread_mutex_unlock(&g_log_mu);
		return 0;
	}

	rnd = ((uint32_t)time(NULL) * 1103515245u) ^ (uint32_t)getpid();
	snprintf(g_log_path, sizeof(g_log_path), "/tmp/trainer_daemon_%u.log", rnd);
	g_log_fp = fopen(g_log_path, "w");
	if (g_log_fp == NULL) {
		snprintf(g_log_path, sizeof(g_log_path), "/tmp/trainer_daemon_%u.log", (unsigned int)getpid());
		g_log_fp = fopen(g_log_path, "w");
	}
	if (g_log_fp == NULL) {
		g_log_path[0] = '\0';
		pthread_mutex_unlock(&g_log_mu);
		return -1;
	}
	setvbuf(g_log_fp, NULL, _IOLBF, 0);
	pthread_mutex_unlock(&g_log_mu);
	return 0;
}

void trainer_log_shutdown(void)
{
	pthread_mutex_lock(&g_log_mu);
	if (g_log_fp != NULL) {
		fflush(g_log_fp);
		fclose(g_log_fp);
		g_log_fp = NULL;
	}
	pthread_mutex_unlock(&g_log_mu);
}

const char *trainer_log_path(void)
{
	const char *out = NULL;

	pthread_mutex_lock(&g_log_mu);
	if (g_log_path[0] != '\0') {
		out = g_log_path;
	}
	pthread_mutex_unlock(&g_log_mu);
	return out;
}

void trainer_log_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vwrite("INFO", fmt, ap);
	va_end(ap);
}

void trainer_log_live(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vwrite("LIVE", fmt, ap);
	va_end(ap);
}

void trainer_log_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vwrite("ERROR", fmt, ap);
	va_end(ap);
}
