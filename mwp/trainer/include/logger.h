#ifndef TRAINER_LOGGER_H
#define TRAINER_LOGGER_H

int trainer_log_init(void);
void trainer_log_shutdown(void);
const char *trainer_log_path(void);

void trainer_log_info(const char *fmt, ...);
void trainer_log_live(const char *fmt, ...);
void trainer_log_error(const char *fmt, ...);

#endif
