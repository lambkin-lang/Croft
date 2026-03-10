#include "tests/generated/wasi_clocks_monotonic_clock.h"
#include "tests/generated/wasi_clocks_timezone.h"

int main(void)
{
    SapWitClocksDatetime when = {0};
    SapWitClocksTimezoneCommand timezone_command = {0};
    SapWitClocksTimezoneReply timezone_reply = {0};
    SapWitClocksMonotonicClockCommand monotonic_command = {0};
    SapWitClocksMonotonicClockReply monotonic_reply = {0};
    SapWitClocksPollableResource pollable = 0;

    when.seconds = 1;
    timezone_command.case_tag = SAP_WIT_CLOCKS_TIMEZONE_COMMAND_DISPLAY;
    timezone_command.val.display = when;
    timezone_reply.case_tag = SAP_WIT_CLOCKS_TIMEZONE_REPLY_DISPLAY;
    monotonic_command.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_COMMAND_SUBSCRIBE;
    monotonic_reply.case_tag = SAP_WIT_CLOCKS_MONOTONIC_CLOCK_REPLY_SUBSCRIBE;
    monotonic_reply.val.subscribe = pollable;

    (void)timezone_command;
    (void)timezone_reply;
    (void)monotonic_command;
    (void)monotonic_reply;
    return 0;
}
