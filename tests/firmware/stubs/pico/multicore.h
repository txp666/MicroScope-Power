#pragma once
void multicore_lockout_victim_init(void);
void multicore_launch_core1(void (*entry)(void));
void multicore_lockout_start_blocking(void);
void multicore_lockout_end_blocking(void);
