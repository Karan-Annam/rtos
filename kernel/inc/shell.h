#ifndef SHELL_H
#define SHELL_H

/* The shell task body: create it like any task, at any priority you like:
 *   os_task_create(shell_task, 0, 8, "shell");
 * Reads the board console (non-blocking + sleep), understands:
 *   help ps stats uptime echo suspend resume kill  */
void shell_task(void *arg);

#endif /* SHELL_H */
