/* A small interactive shell -- the RTOS equivalent of a hello world that
 * talks back. Runs as an ordinary task; everything it shows you (ps, stats)
 * comes from the public introspection API, no kernel hooks. */

#include <string.h>
#include "os.h"
#include "board.h"
#include "kprintf.h"
#include "shell.h"

#define LINE_MAX 80

static const char *state_names[] = {
    "unused", "ready", "running", "blocked", "suspend", "zombie",
};

static void cmd_help(void)
{
    console_write(
        "commands:\n"
        "  ps            task table (state, prio, stack use, switches)\n"
        "  stats         uptime + cpu load\n"
        "  uptime        tick count\n"
        "  echo <text>   say it back\n"
        "  suspend <id>  freeze a task\n"
        "  resume <id>   thaw a task\n"
        "  kill <id>     delete a task\n"
        "  help          this\n");
}

static void cmd_ps(void)
{
    os_printf("id name       state    prio   stack     switches\n");
    for (int i = 0; i < OS_MAX_TASKS; i++) {
        os_tcb_t *t = os_task_by_index(i);
        if (!t)
            continue;
        char prio[8], stack[16];
        if (t->curr_prio != t->base_prio)   /* boosted: show both */
            os_snprintf(prio, sizeof prio, "%d>%d",
                        t->base_prio, t->curr_prio);
        else
            os_snprintf(prio, sizeof prio, "%d", t->curr_prio);
        os_snprintf(stack, sizeof stack, "%lu/%lu",
                    os_stack_high_water(t), t->stack_words);
        os_printf("%2d %-10s %-8s %-6s %-9s %lu\n",
                  t->id, t->name, state_names[t->state], prio, stack,
                  t->nswitches);
    }
}

static void cmd_stats(void)
{
    uint32_t up = os_tick_count();
    uint32_t idle = os_idle_ticks();
    uint32_t load = up ? (up - idle) * 100u / up : 0;
    os_printf("uptime %lu ticks | idle %lu | cpu load %lu%%\n",
              up, idle, load);
}

static os_tcb_t *arg_to_task(const char *s)
{
    int id = 0;
    if (*s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9')
        id = id * 10 + (*s++ - '0');
    return os_task_by_index(id);
}

static void run_line(char *line)
{
    /* split off the first word */
    char *arg = line;
    while (*arg && *arg != ' ')
        arg++;
    if (*arg)
        *arg++ = '\0';
    while (*arg == ' ')
        arg++;

    if (!*line)
        return;
    if (!strcmp(line, "help")) {
        cmd_help();
    } else if (!strcmp(line, "ps")) {
        cmd_ps();
    } else if (!strcmp(line, "stats")) {
        cmd_stats();
    } else if (!strcmp(line, "uptime")) {
        os_printf("%lu ticks\n", os_tick_count());
    } else if (!strcmp(line, "echo")) {
        os_printf("%s\n", arg);
    } else if (!strcmp(line, "suspend")) {
        os_tcb_t *t = arg_to_task(arg);
        os_printf(t && os_task_suspend(t) == OS_OK ? "ok\n" : "no such task\n");
    } else if (!strcmp(line, "resume")) {
        os_tcb_t *t = arg_to_task(arg);
        os_printf(t && os_task_resume(t) == OS_OK ? "ok\n" : "no such task\n");
    } else if (!strcmp(line, "kill")) {
        os_tcb_t *t = arg_to_task(arg);
        if (t == os_task_self()) {
            os_printf("the shell declines to kill itself\n");
        } else {
            os_printf(t && os_task_delete(t) == OS_OK ? "ok\n" : "no such task\n");
        }
    } else {
        os_printf("unknown command '%s' (try help)\n", line);
    }
}

void shell_task(void *arg)
{
    (void)arg;
    static char line[LINE_MAX];
    int pos = 0;

    os_printf("\nrtos shell -- type 'help'\n> ");

    for (;;) {
        int c = console_getc_nonblock();
        if (c < 0) {
            os_sleep_ms(10);           /* poll politely, don't hog */
            continue;
        }
        if (c == '\r' || c == '\n') {
            console_write("\n");
            line[pos] = '\0';
            run_line(line);
            pos = 0;
            console_write("> ");
        } else if (c == 0x7F || c == '\b') {
            if (pos > 0) {
                pos--;
                console_write("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7F && pos < LINE_MAX - 1) {
            line[pos++] = (char)c;
            console_putc((char)c);     /* echo */
        }
    }
}
