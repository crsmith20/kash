#include "history.h"

#include <stdio.h>

void print_history(struct history_entry *history[HIST_MAX]) {
	int i;
	char* cmd;
	char* time;
	double run_time;
	unsigned long id; 
	while (history[i] != NULL && i < HIST_MAX) {
		cmd = history[i]->cmd;
		time = history[i]->time;
		run_time = history[i]->run_time;
		id = history[i]->cmd_id;
		printf("[%lu|%s|%.2f] %s\n", id, time, run_time, cmd);
		i++;
	}
}

