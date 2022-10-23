#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// define states of a task
#define RUNNING 0
#define READY 1
#define SLEEP 2
#define USERINPT 3
#define BLOCKED 4
#define EXIT 5
// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  //   b. If the task is sleeping, when should it wake up?
  //   c. If the task is waiting for another task, which task is it waiting for?
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
  // This field stores all the state required to switch back to this task
  ucontext_t context;

  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;

  // Keeps track of whether the task is running (0), ready (1), sleeping (2),
  // waiting for user input (3), blocked (4), or exit (5).
  int state;

  // Record the time the task should wake up
  size_t wakeupTime;

  // id of the task we are waiting for.
  task_t waitingTask_id;

  // check if it is blocked waiting for user input.
  int userInput;
} task_info_t;

int current_task = 0;          //< The handle of the currently-executing task
int num_tasks = 1;             //< The number of tasks created so far
task_info_t tasks[MAX_TASKS];  //< Information for every task

/**
 * Check the state of every task and perform the actions that correspond to the following state the
 * task is currently in.
 */
void checkTasks() {
  // increment current task by 1
  int index = current_task + 1;
  // declared temporary to store previous task for swapcontext later
  int temp;
  while (true) {
    // traverses through num_tasks and does not go beyond the limit
    index = (index) % num_tasks;
    // when running just return and swap the current tasks
    if (tasks[index].state == RUNNING) {
      return;
    } else if (tasks[index].state == SLEEP) {      // verifying sleep state
      if (time_ms() >= tasks[index].wakeupTime) {  // if it is time for task to wake up
        tasks[index].state = RUNNING;              // change task state to running
        temp = current_task;                       // store current task within temp
        current_task = index;                      // update current_task
        break;
      }
    } else if (tasks[index].state == USERINPT) {  // verifying user input state
      int userIn = getch();                       // get user input
      if (userIn != ERR) {                        // check if errors
        tasks[index].userInput = userIn;          // store user input
        tasks[index].state = RUNNING;             // change task state to running
        temp = current_task;
        current_task = index;
        break;
      }
    } else if (tasks[index].state == BLOCKED) {                // verifying block state
      if (tasks[tasks[index].waitingTask_id].state == EXIT) {  // if waiting task state is exit
        tasks[index].state = READY;                            // change task state to ready
        temp = current_task;
        current_task = index;
        break;
      }
    } else if (tasks[index].state == READY) {  // verifying ready state
      tasks[index].state = RUNNING;            // change task state to running
      temp = current_task;
      current_task = index;
      break;
    }
    index++;  // increment index to next task
  }
  // swap task contexts
  swapcontext(&tasks[temp].context, &tasks[current_task].context);
}

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functions in this file.
 */
void scheduler_init() {
  // TODO: Initialize the state of the scheduler
  tasks[0].state = RUNNING;
}

/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  // change task state to exit
  tasks[current_task].state = EXIT;
  checkTasks();
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;

  // Set the task handle to this index, since task_t is just an int
  *handle = index;

  // We're going to make two contexts: one to run the task, and one that runs at the end of the task
  // so we can clean up. Start with the second

  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);

  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;

  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);

  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);

  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;

  // Now set the uc_link field, which sets things up so our task will go to the exit context when
  // the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;

  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);

  // make new task state to be ready
  tasks[index].state = READY;
  // setting the waiting task id to be -1
  tasks[index].waitingTask_id = -1;
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // TODO: Block this task until the specified task has exited.
  // change task state to blocked
  tasks[current_task].state = BLOCKED;
  // set watinging task id to be the handle
  tasks[current_task].waitingTask_id = handle;
  checkTasks();
}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 *
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  // TODO: Block this task until the requested time has elapsed.
  // Hint: Record the time the task should wake up instead of the time left for it to sleep. The
  // bookkeeping is easier this way.
  // change task state to sleep
  tasks[current_task].state = SLEEP;
  // set the tasks wake up time to be actual time with the inputed waiting time
  tasks[current_task].wakeupTime = time_ms() + ms;
  checkTasks();
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was available.
  // Otherwise, getch() will returns the character code that was read.
  // change task state to user input
  tasks[current_task].state = USERINPT;
  checkTasks();
  // returns the character code that was read
  return tasks[current_task].userInput;
}
