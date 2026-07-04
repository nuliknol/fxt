#include "core_api.h"
#include "logger.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	const char *bind_ip = "0.0.0.0";
	uint16_t port = 80;
	const char *storage_root = "trainer_data";
	int rc;

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (argc > 1) {
		bind_ip = argv[1];
	}
	if (argc > 2) {
		port = (uint16_t)atoi(argv[2]);
	}
	if (argc > 3) {
		storage_root = argv[3];
	}

	if (trainer_log_init() != 0) {
		fprintf(stderr, "warning: failed to create trainer log file in /tmp\n");
	}
	if (trainer_log_path() != NULL) {
		fprintf(stdout, "trainer_daemon log file: %s\n", trainer_log_path());
		fflush(stdout);
	}

	trainer_log_info("startup bind_ip=%s port=%u storage_root=%s", bind_ip, (unsigned int)port, storage_root);
	rc = core_init(storage_root);
	if (rc != 0) {
		trainer_log_error("core_init failed rc=%d", rc);
		fprintf(stderr, "core_init failed: %d\n", rc);
		trainer_log_shutdown();
		return 1;
	}

	fprintf(stdout, "trainer_daemon listening on %s:%u\n", bind_ip, (unsigned int)port);
	fflush(stdout);
	rc = core_http_serve(bind_ip, port);
	core_shutdown();
	if (rc != 0) {
		trainer_log_error("server loop failed rc=%d", rc);
		fprintf(stderr, "server error: %d\n", rc);
		trainer_log_shutdown();
		return 1;
	}
	trainer_log_info("shutdown clean");
	trainer_log_shutdown();
	return 0;
}
