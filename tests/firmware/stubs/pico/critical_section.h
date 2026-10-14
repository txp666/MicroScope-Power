#pragma once
#include <pthread.h>
typedef struct { pthread_mutex_t mutex; } critical_section_t;
void critical_section_init(critical_section_t *section);
void critical_section_enter_blocking(critical_section_t *section);
void critical_section_exit(critical_section_t *section);
