#ifndef _HISTORY_H_
#define _HISTORY_H_

#define HIST_MAX 100

struct history_entry {
    unsigned long cmd_id;
    double run_time;
    char time[6];
    char cmd[HIST_MAX];
};

void print_history(struct history_entry *history[HIST_MAX]);

#endif
